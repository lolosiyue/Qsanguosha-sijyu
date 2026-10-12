#include "room-runtime.h"
#include "ai-strategy-round56-maxcards.h"
#include "ai-strategy-round56-patterns.h"
#include <cstdio>
#include "ai-strategy-round53-ainoto.h"
#include "ai-strategy-round56-projection.h"
#include "ai-strategy-round53-manjuan.h"
#include "ai-strategy-round54-shebian.h"
#include "ai-strategy-round54-cardchoice.h"
#include "ai-strategy-round51-notified-costs.h"
#include "ai-strategy-round49-extra-response.h"
#include "ai-strategy-round47-xufa-query-scope.h"
#include "ai-strategy-round47-heqia.h"
#include "ai-strategy-round47b-fate-receipt.h"
#include "ai-strategy-round47b-leiluan.h"
#include "ai-strategy-round48-response.h"
#include "ai-strategy-round49e-response.h"
#include "ai-strategy-round49d-response.h"
#include "ai-strategy-round49b-response.h"
#include "ai-strategy-round49-response.h"
#include "ai-strategy-round46-jianman.h"
#include "ai-strategy-round45-xufa-slot.h"
#include "ai-strategy-round53-xufa-response.h"
#include "ai-strategy-round53-xuanzhu-slot.h"
#include "ai-strategy-round53-xuanzhu-response.h"
#include "ai-strategy-round44-response.h"
#include "ai-strategy-round43-response.h"
#include "ai-strategy-round50-htms-events.h"
#include "ai-strategy-round43-context.h"
#include "ai-strategy-round47e-choice.h"
#include "ai-strategy-round49-choice.h"
#include "ai-strategy-round49-yitian.h"
#include "ai-strategy-round49-qhturn.h"
#include "ai-strategy-round49-qixi.h"
#include "ai-strategy-round49-current.h"
#include "ai-strategy-round49-gongjian.h"
#include "ai-strategy-round49-names.h"
#include "ai-strategy-round49-record-constructors.h"
#include "ai-strategy-round49-bools.h"
#include "ai-strategy-round49-notified.h"
#include "ai-strategy-round49-notified-context.h"
#include "ai-strategy-round49-zero.h"
#include "ai-strategy-round49-extra.h"
#include "ai-strategy-round49-bozhi.h"
#include "ai-strategy-round49-maotu-yinhu.h"
#include "ai-strategy-round49-guhuo.h"
#include "ai-strategy-round49-shuangxiong.h"
#include "ai-strategy-round49-lianhuan.h"
#include "ai-strategy-round49-xinhuo.h"
#include "ai-strategy-round42-context.h"
#include "ai-strategy-round42-response.h"
#include "ai-strategy-round41-response.h"
#include "ai-strategy-round42-choice.h"
#include "ai-strategy-round43-choice.h"
#include "ai-strategy-round44-choice.h"
#include "ai-strategy-round45-context.h"
#include "ai-strategy-round50-yuri-events.h"
#include "ai-strategy-round45-choice.h"
#include "ai-strategy-round40-context.h"
#include "ai-strategy-round47d-dapo-context.h"
#include "ai-strategy-round41-context.h"
#include "ai-strategy-round35-response.h"
#include "ai-strategy-round34-choice.h"
#include "ai-strategy-round35-choice.h"
#include "ai-strategy-round38-choice.h"
#include "ai-strategy-round39-choice.h"
#include "ai-strategy-round40-choice.h"
#include "ai-strategy-round39-context.h"
#include "ai-strategy-round36-formation.h"
#include "ai-strategy-round36-response-wire.h"
#include "ai-strategy-round33-common.h"
#include "ai-strategy-round33-response.h"
#include "ai-strategy-round33-choice.h"
#include "ai-strategy-round29-original.h"
#include "ai-strategy-round29-choice.h"
#include "ai-strategy-round30-choice.h"
#include "ai-strategy-round31-choice.h"
#include "ai-strategy-round31-intention.h"
#include "ai-strategy-round31-response.h"
#include "ai-strategy-round31-planning.h"
#include "ai-strategy-round30-planning.h"
#include "ai-strategy-round28-response.h"
#include "ai-strategy-round27-response.h"
#include "ai-strategy-round26-response.h"
#include "ai-strategy-round26-helpers.h"
#include "ai-strategy-round27-jieyingh.h"
#include "ai-strategy-round29-response.h"
#include "ai-strategy-round39-response.h"
#include "ai-strategy-round40-planning.h"
#include "ai-strategy-round41-planning.h"
#include "ai-strategy-round42-planning.h"
#include "ai-strategy-round43-planning.h"
#include "ai-strategy-round45-planning.h"
#include "ai-strategy-round48-planning.h"
#include "ai-strategy-round49h-planning.h"
#include "ai-strategy-round49n-planning.h"
#include "ai-strategy-round49p-planning.h"
#include "ai-strategy-round49q-planning.h"
#include "ai-strategy-round50-planning.h"
#include "ai-strategy-round51-collateral.h"
#include "ai-strategy-round53-collateral.h"
#include "ai-strategy-round53-lianzhan.h"
#include "ai-strategy-round53-mingfa.h"
#include "ai-strategy-round53-gengdu.h"
#include "ai-strategy-round53-zhuning.h"
#include "ai-strategy-round53-diou.h"
#include "ai-strategy-round53-tyfenwu.h"
#include "ai-strategy-round53-tyqingkou.h"
#include "ai-strategy-round53-tychende.h"
#include "ai-strategy-round52-rangyi.h"
#include "ai-strategy-round49r-planning.h"
#include "ai-strategy-round49o-planning.h"
#include "ai-strategy-round49g-planning.h"
#include "ai-strategy-round49f-planning.h"
#include "ai-strategy-round49e-planning.h"
#include "ai-strategy-round49d-planning.h"
#include "ai-strategy-round49c-planning.h"
#include "ai-strategy-round49b-planning.h"
#include "ai-strategy-round49j-planning.h"
#include "ai-strategy-round49-extra-planning.h"
#include "ai-strategy-round49-planning.h"
#include "ai-strategy-round44-range.h"
#include "ai-strategy-range-query.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-round43-common.h"
#include "ai-strategy-round45-inovation.h"
#include "ai-strategy-round47-xiedou.h"
#include "ai-strategy-round47c-retrial.h"
#include "ai-strategy-round47-sijyu.h"
#include "ai-strategy-round51-jijiao.h"
#include "ai-strategy-round52-olsp.h"
#include "ai-strategy-round47-fengyi.h"
#include "ai-strategy-round45-inovation-state.h"
#include "ai-strategy-round45-inovation-providers.h"
#include "ai-strategy-round30-response.h"
#include "ai-strategy-round24-original.h"
#include "ai-strategy-round24-choice.h"
#include "ai-strategy-round24-response.h"
#include "ai-strategy-preview-authority.h"
#include "ai-strategy-round21-readonly.h"
#include "ai-strategy-round21-original.h"
#include "ai-strategy-round21-response.h"
#include "ai-strategy-round21-planning.h"
#include "ai-strategy-round21-choice.h"
#include "ai-strategy-round22-olclan-plan.h"
#include "ai-strategy-round22-olclan-actions.h"
#include "ai-strategy-round23-qiuxin.h"
#include "ai-strategy-round24-feidiy.h"
#include "ai-strategy-round25-planning.h"
#include "ai-strategy-round26-planning.h"
#include "ai-strategy-round27-planning.h"
#include "ai-strategy-round28-planning.h"
#include "ai-strategy-round25-helpers.h"
#include "ai-strategy-round25-choice.h"
#include "ai-strategy-round26-choice.h"
#include "ai-strategy-round27-choice.h"
#include "ai-strategy-round28-choice.h"
#include "ai-strategy-round25-response.h"
#include "ai-strategy-round25-original.h"
#include "ai-strategy-round26-original.h"
#include "ai-strategy-round27-original.h"
#include "ai-strategy-round36-planning.h"
#include "ai-strategy-round28-original.h"
#include "ai-strategy-round22-olclan-observations.h"
#include "ai-strategy-round23-olclan-planning.h"
#include "ai-strategy-round22-choice.h"
#include "ai-strategy-round23-choice.h"
#include "ai-strategy-response-observer.h"
#include "ai-strategy-choice-context.h"
#include "ai-strategy-construction-query.h"
#include "ai-strategy-construction-context.h"
#include "ai-strategy-response-catalog.h"
#include "ai-strategy-public-deck.h"
#include "ai-owner-range-query-scope.h"
#include "ai-strategy-round25-distance.h"
#include "ai-strategy-scarlet-context.h"
#include "ai-strategy-response-context.h"
#include "ai-strategy-retrial-context.h"
#include "ai-strategy-judge-context.h"
#include "ai-strategy-conditional-prototypes.h"
#include "ai-strategy-source-prototypes.h"
#include "ai-strategy-public-action-flags.h"
#include "ai-strategy-public-disclosure.h"
#include "ai-strategy-nested-cardask.h"
#include "notified-pile-query-scope.h"
#include "skill-generated-card.h"
#include "external-agent.h"
#include "ai-decision-coordinator.h"
#include "ai-strategy-round47-olclan-values.h"
#include "ai-strategy-round47-olclan-scalar.h"
#include "ai-strategy-round47-planning-slots.h"
#include "ai-strategy-round46-planning-slots.h"
#include "ai-strategy-choice-continuity.h"
#include "ai-strategy-round33-planning.h"
#include "ai-strategy-round34-planning.h"
#include "qt-collection-utils.h"
#include "ai-runtime.h"
#include "ai-strategy-projection.h"
#include "ai-strategy-use-dispatch.h"
#include "ai-strategy-card-identity.h"
#include "ai-strategy-animecard.h"
#include "ai-strategy-newstar.h"
#include "ai-strategy-olclan.h"
#include "ai-strategy-dragon-events.h"
#include "ai-strategy-public-disclosure.h"
#include "card-lifetime-manager.h"
#include "ai-strategy-yczh2016.h"
#include "ai-strategy-yczh2017.h"
#include "ai-strategy-random.h"
#include "ai-strategy-events.h"
#include "ai-strategy-owner.h"
#include "ai-strategy-yjcm2012.h"
#include "ai-strategy-yjcm2014.h"
#include "ai-strategy-yjcm2015.h"
#include "ai-strategy-history-context.h"
#include "card-lifetime-manager.h"
#include "lua.hpp"
#include "lua-wrapper.h"

#include "engine.h"
#include "room.h"
#include "roomthread.h"
#include "server-info.h"
#include "settings.h"
#include "skill-runtime-coordinator.h"
#include "skill-declaration.h"
#include "standard.h"
#include "general.h"
#include "room-state.h"
#include "maneuvering.h"
#include "wrapped-card.h"

#include <cmath>
#include <limits>
#include <memory>
#include <typeinfo>
#include <QJsonArray>
#include <QDateTime>
#include <QUuid>
#include "ai-probe.h"
#include "skill-set-generation.h"
#include <QElapsedTimer>
#include <QLoggingCategory>

namespace QSanEngine { extern thread_local Player *Self; }

namespace {
// The event log is bounded so a long game cannot grow the snapshot without limit.
const int AiEventLogLimit = 64;


// Taoxi is a notification-backed borrowed hand card, not a transfer of ownership.
// Require both the issued disclosure and the still-live native borrowing witness.
static bool liveTaoxiBorrow(const Room &room, const ServerPlayer *viewer, const AIWorldView &world, int id)
{
    const auto row = world.strategyFacts.value("yjcm2015").toObject().value("taoxi_borrowed").toObject();
    if (!viewer || row.value("card_id").toInt(-1) != id || !row.value("card").isObject()
        || !viewer->hasFlag("TaoxiRecord") || !viewer->getTag("TaoxiHere").toBool()) return false;
    bool valid = false;
    if (viewer->getTag("TaoxiId").toInt(&valid) != id || !valid) return false;
    const auto *owner = room.getCardOwner(id);
    return owner && owner != viewer && owner->objectName() == row.value("owner").toString()
        && room.getCardPlace(id) == Player::PlaceHand && owner->handCards().contains(id);
}

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
    completeCoverage = visit() && stableProjection();
    if (!completeCoverage) combinations.clear();
}

static AICardCandidateView makeAICardCandidate(Room &room, ServerPlayer *player,
                                               const Card *card, Card::HandlingMethod method,
                                               AIRequest::DecisionKind kind,
                                               const QString &pattern, int &projectionBudget,
                                               bool sourceQueryOnly = false, bool *sourceQueriesComplete = nullptr)
{
    if (sourceQueriesComplete) *sourceQueriesComplete = false;
    AICardCandidateView candidate;
    const auto queryRevision = room.roomRuntime()->stateRevision();
    const auto queryGeneration = SkillSet::generation();
    const auto queryDefinitions = room.roomRuntime()->definitions().skillDefinitionVersion();
    if (!player || !card)
        return candidate;
    const auto queryAlive = sourceQueryOnly ? room.getAlivePlayers() : QList<ServerPlayer *>();
    const auto queryOwnerFlags = sourceQueryOnly ? player->getFlagList() : QStringList();
    const auto queryCardFlags = sourceQueryOnly ? card->getFlags() : QStringList();
    const Card *queryReal = sourceQueryOnly ? card->getRealCard() : nullptr;
    const auto queryRealFlags = queryReal ? queryReal->getFlags() : QStringList();
    QList<QStringList> queryTargetFlags;
    for (const auto *target : queryAlive) queryTargetFlags << target->getFlagList();
    const auto queryFlagsStable = [&]() {
        if (room.getAlivePlayers() != queryAlive || player->getFlagList() != queryOwnerFlags
            || card->getFlags() != queryCardFlags || card->getRealCard() != queryReal
            || (queryReal && queryReal->getFlags() != queryRealFlags)) return false;
        for (int i = 0; i < queryAlive.size(); ++i)
            if (queryAlive.at(i)->getFlagList() != queryTargetFlags.at(i)) return false;
        return true;
    };
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
        candidate.available = matched.isEmpty() || (kind == AIRequest::UseCard
            ? Sanguosha->matchExpPattern(matched, player, card)
            : Sanguosha->matchPattern(matched, player, card));
    }
    candidate.limited = player->isCardLimited(card, method);
    candidate.jilei = player->isJilei(card);
    candidate.canUseTargets = projectAICanUseTargets(room, player, card);
    if (--projectionBudget >= 0) {
        candidate.extraTargetCount = Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, player, card);
        candidate.extraTargetKnown = true;
    }
    for (ServerPlayer *target : room.getAlivePlayers()) {
        if (--projectionBudget < 0) break;
        candidate.targetFilterEmpty.insert(target->objectName(), card->targetFilter({}, target, player));
        if (--projectionBudget < 0) break;
        candidate.extraTargetByPlayer.insert(target->objectName(),
            Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, player, card, target));
    }
    if ((!candidate.available || candidate.limited) && !sourceQueryOnly) {
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
    if (sourceQueryOnly) {
        // Qiuxin's source asks finite predicate queries, not the exponential
        // set of all ordered target sequences. Never claim complete coverage.
        if (--projectionBudget < 0) return candidate;
        candidate.targetFixed = card->targetFixed();
        if (--projectionBudget < 0) return candidate;
        candidate.feasibleWithNoTarget = card->targetsFeasible({}, player);
        for (ServerPlayer *target : room.getAlivePlayers()) {
            if (--projectionBudget < 0) return candidate;
            int votes = 0;
            card->targetFilter({}, target, player, votes);
            if (votes <= 0) continue;
            if (--projectionBudget < 0) return candidate;
            if (room.isProhibited(player, target, card, {})) continue;
            candidate.legalTargets << target->objectName();
            if (votes > 1) candidate.maxVotes.insert(target->objectName(), votes);
        }
    } else {
        describeCardTargets(room, player, card, candidate.targetFixed,
                            candidate.feasibleWithNoTarget, candidate.completeCoverage,
                            candidate.legalTargets, candidate.maxVotes,
                            candidate.targetCombinations, projectionBudget);
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
    if (sourceQueryOnly && sourceQueriesComplete)
        *sourceQueriesComplete = projectionBudget >= 0 && queryFlagsStable()
            && queryRevision == room.roomRuntime()->stateRevision()
            && queryGeneration == SkillSet::generation()
            && queryDefinitions == room.roomRuntime()->definitions().skillDefinitionVersion()
            && (!(standardAoe || standardGlobal) || candidate.affectedTargetsKnown);
    return candidate;
}


// Classification metadata only: no event-side payment ticket or legacy AI callback.
static QJsonObject eventConversionPolicy(Room &room, ServerPlayer *viewer)
{
    lua_State *state = room.roomRuntime()->lua().state();
    const int top = lua_gettop(state);
    const auto restore = qScopeGuard([&] { lua_settop(state, top); });
    QList<const Skill *> skills;
    for (const Skill *skill : viewer->getSkillList(true))
        if (skill && (skill->isLordSkill() ? viewer->hasLordSkill(skill->objectName()) : viewer->hasSkill(skill)))
            skills << skill;
    bool complete = true;
    if (viewer->hasSkill("weidi")) {
        const ServerPlayer *lord = room.getLord();
        if (lord && lord != viewer) {
            if (ServerInfo.EnableHegemony && !lord->hasShownAllGenerals() && lord->isAlive()) complete = false;
            else for (const Skill *skill : lord->getSkillList(true))
                if (skill && skill->isLordSkill()) skills << skill;
        }
    }
    lua_getglobal(state, "sgs");
    if (!lua_istable(state, -1)) return {{"complete", false}};
    lua_getfield(state, -1, "ai_view_as");
    if (!lua_istable(state, -1)) return {{"complete", false}};
    const int registry = lua_gettop(state);
    QJsonObject byClass;
    for (const auto &entry : QList<QPair<QString, QString>>{{"Slash","slash"}, {"Jink","jink"},
            {"Peach","peach"}, {"Analeptic","analeptic"}, {"Nullification","nullification"},
            {"DelayedTrick","DelayedTrick"}}) {
        QString enabledPattern = entry.second;
        if ((entry.first == "Peach" || entry.first == "Analeptic") && room.getCurrentDyingPlayer() == viewer)
            enabledPattern = "peach+analeptic";
        QJsonArray rows;
        for (const Skill *skill : skills) {
            lua_getfield(state, registry, skill->objectName().toUtf8().constData());
            const bool hook = lua_isfunction(state, -1);
            lua_pop(state, 1);
            const ViewAsSkill *vs = ViewAsSkill::parseViewAsSkill(skill);
            // Match source short circuit: disabled/unregistered callbacks must
            // not invoke arbitrary producer enablement for unrelated skills.
            const bool enabled = hook && vs && vs->isEnabledAtResponse(viewer, enabledPattern);
            rows.append(QJsonObject{{"skill_name", skill->objectName()}, {"view_as_exists", vs != nullptr},
                {"enabled_at_response", enabled}, {"legacy_hooks", QJsonObject{{"ai_view_as", hook}}}});
        }
        byClass.insert(entry.first, QJsonObject{{"complete", complete}, {"card_name", entry.second}, {"skills", rows}});
    }
    return {{"complete", complete}, {"by_class", byClass}};
}

static QString legacySortKey(const Card *card)
{
    QString key = card->toString();
    if (card->isKindOf("ActiveSkillCard")) {
        key += QLatin1Char('|') + card->getSkillName();
        if (card->getSkillInstanceId() > 0)
            key += QLatin1Char('#') + QString::number(card->getSkillInstanceId());
    }
    return key;
}

static AICardView makeAICardView(const Card *card, bool observeIdentity = false)
{
    AICardView view;
    if (!card)
        return view;
    // Physical toString is just its already disclosed ID; never serialize a
    // foreign virtual card's hidden costs or user string here.
    if (!card->isVirtualCard()) {
        view.legacyStringKey = card->toString();
        view.legacySortKey = legacySortKey(card);
    }
    view.nativeIdentity = globalCardLifetimeManager().liveToken(card);
    // Disclosed own hand/pile and public discard faces opt in.
    // Capture the actual object, before deriving its face; do not synthesize
    // identity from an ID or merge WrappedCard with its inner real card.
    if (observeIdentity && !view.nativeIdentity)
        view.nativeIdentity = globalCardLifetimeManager().observeCard(const_cast<Card *>(card));
    view.cardId = card->getId();
    view.effectiveId = card->getEffectiveId();
    view.objectName = card->objectName();
    view.nameLength = card->nameLength();
    view.className = card->getClassName();
    view.sourceSkillName = card->getSourceSkillName();
    view.skillNames = card->getSkillNames();
    view.canRecast = card->canRecast();
    view.willThrow = card->willThrow();
    view.suit = int(card->getSuit());
    view.number = card->getNumber();
    view.skillName = card->getSkillName(false);
    view.red = card->isRed();
    view.black = card->isBlack();
    view.kindOfNames = card->getKindOfNames();
    view.typeId = int(card->getTypeId());
    if (const auto *equip = dynamic_cast<const EquipCard *>(card->getRealCard())) {
        view.equipSlot = int(equip->location());
        view.equipOccupyLocations = equip->getOccupyLocations();
    }
    if (const auto *weapon = dynamic_cast<const Weapon *>(card->getRealCard()))
        view.weaponRange = weapon->getRange();
    view.handlingMethod = int(card->getHandlingMethod());
    view.virtualCard = card->isVirtualCard();
    view.targetFixed = card->targetFixed();
    view.damageCard = card->isDamageCard(); view.singleTarget = card->isSingleTargetCard();
    view.subcardIds = card->getSubcards();
    return view;
}

static void projectOwnCardState(AICardView &view, const ServerPlayer *owner, int &budget)
{
    const Card *card = Sanguosha->getCard(view.effectiveId);
    if (!owner || !card || card->isVirtualCard()) return;
    view.ownerStateKnown = true;
    view.skillNames = card->getSkillNames();
    view.skillNamesComplete = true;
    view.locked = owner->isLocked(card);
    view.jilei = owner->isJilei(card);
    view.usingCard = card->hasFlag("using");
    view.killOff = card->hasFlag("AIGlobal_KillOff");
    const auto targets = card->targetFixed() ? QList<ServerPlayer *>{const_cast<ServerPlayer *>(owner)}
                                             : owner->getRoom()->getAlivePlayers();
    if ((card->targetFixed() && card->getTypeId() < Card::TypeTrick) || card->isDamageCard()) {
        for (const ServerPlayer *target : targets) {
            if (--budget < 0) break;
            view.targetResidue.insert(target->objectName(),
                Sanguosha->correctCardTarget(TargetModSkill::Residue, owner, card, target));
        }
    }
    if (const Card *engineCard = Sanguosha->getEngineCard(view.effectiveId))
        view.engineClassName = engineCard->getClassName();
}

class ResponseNativeScope
{
public:
    ResponseNativeScope(Room &room, ServerPlayer *player, CardUseStruct::CardUseReason reason, const QString &pattern)
        : state(room.roomState()), oldSelf(QSanEngine::Self), oldPlayer(state->getCurrentPlayer()),
          oldReason(state->getCurrentCardUseReason()), oldPattern(state->getCurrentCardUsePattern())
    {
        setEngineSelf(player); state->setCurrentPlayer(player);
        state->setCurrentCardUseReason(reason); state->setCurrentCardUsePattern(pattern);
    }
    ~ResponseNativeScope()
    {
        state->setCurrentCardUsePattern(oldPattern); state->setCurrentCardUseReason(oldReason);
        state->setCurrentPlayer(oldPlayer); setEngineSelf(oldSelf);
    }
private:
    RoomState *state;
    Player *oldSelf;
    Player *oldPlayer;
    CardUseStruct::CardUseReason oldReason;
    QString oldPattern;
};

// Reproduce a declaration dialog's native read contract without setTag, signals,
// lifetime registrations or room revision changes. Only metadata-validated values
// can reach the thread-local owner/key overlay; never apply a policy-provided tag.
class ResponseDeclarationScope final {
public:
    bool prepare(const QString &skillName, ServerPlayer *player,
                 CardUseStruct::CardUseReason reason, const QString &pattern,
                 const SkillInstanceRef &activationRef, const QString &proposed,
                 bool explicitDeclaration)
    {
        m_session.reset(new SkillDeclarationSession(skillName, player, reason, pattern,
            Sanguosha->getBanPackages(), 0, activationRef));
        if (!m_session->supported() || !m_session->active()) return true;
        const auto validated = m_session->validate(proposed);
        if (!validated.accepted) return !explicitDeclaration;
        QVariant value;
        for (const SkillDeclarationCandidate &candidate : m_session->candidates()) {
            if (candidate.value != validated.canonicalValue) continue;
            if (candidate.kind == QLatin1String("card")) {
                const Card *card = m_session->cloneCard(candidate.value);
                if (!card) return false;
                m_declaredCard = card;
                value = QVariant::fromValue(card);
            } else value = candidate.value;
            break;
        }
        if (!value.isValid()) return false;
        m_value = validated.canonicalValue;
        m_overlay.reset(new AITagQueryScope(player,
            SkillDeclarationSession::tagKeyFor(m_session->info(), skillName), value));
        return true;
    }
    QString declaration() const { return m_value; }
    bool retainProduced(const Card *card) {
        if (!card || card != m_declaredCard) return true;
        if (m_session->takeClone(m_value) != card) return false;
        m_declaredCard = nullptr;
        return true;
    }
private:
    // Destruction order matters: remove the overlay before the session releases
    // its private clones and inspects the player's real existing tag.
    std::unique_ptr<SkillDeclarationSession> m_session;
    std::unique_ptr<AITagQueryScope> m_overlay;
    QString m_value;
    const Card *m_declaredCard = nullptr;
};

static QJsonObject responseIdentity(const AiSkillActionContext &action)
{
    return {{"activation_owner", action.activationRef.ownerObjectName},
        {"activation_skill", action.activationRef.key.skillName},
        {"activation_instance", action.activationRef.key.instanceID},
        {"source_owner", action.sourceRef.ownerObjectName},
        {"source_skill", action.sourceRef.key.skillName},
        {"source_instance", action.sourceRef.key.instanceID},
        {"activation_quota_available", action.activationQuotaAvailable},
        {"source_quota_available", action.sourceQuotaAvailable}};
}

static SkillInstanceRef responseRef(const QJsonObject &identity, const QString &prefix)
{
    return SkillInstanceRef(identity.value(prefix + "_owner").toString(),
        SkillInstanceKey(identity.value(prefix + "_skill").toString(),
                         identity.value(prefix + "_instance").toInt()));
}

static QJsonArray responseIds(const QList<int> &ids)
{
    QJsonArray values; for (int id : ids) values.append(id); return values;
}

static QJsonObject responseCandidateJson(const AICardCandidateView &candidate)
{
    QJsonArray combinations;
    for (const QStringList &targets : candidate.targetCombinations)
        combinations.append(QJsonArray::fromStringList(targets));
    QJsonObject filters, votes, canUse, extraTargets;
    for (auto it = candidate.targetFilterEmpty.cbegin(); it != candidate.targetFilterEmpty.cend(); ++it)
        filters.insert(it.key(), it.value());
    for (auto it = candidate.maxVotes.cbegin(); it != candidate.maxVotes.cend(); ++it)
        votes.insert(it.key(), it.value());
    for (auto it = candidate.canUseTargets.cbegin(); it != candidate.canUseTargets.cend(); ++it)
        canUse.insert(it.key(), it.value());
    for (auto it = candidate.extraTargetByPlayer.cbegin(); it != candidate.extraTargetByPlayer.cend(); ++it)
        extraTargets.insert(it.key(), it.value());
    QJsonObject value{{"available", candidate.available && !candidate.limited},
        {"limited", candidate.limited}, {"target_fixed", candidate.targetFixed},
        {"feasible_with_no_target", candidate.feasibleWithNoTarget},
        {"complete_coverage", candidate.completeCoverage},
        {"legal_targets", QJsonArray::fromStringList(candidate.legalTargets)},
        {"target_combinations", combinations}, {"target_filter_empty", filters}, {"max_votes", votes}, {"can_use_targets", canUse}, {"extra_target_by_player", extraTargets}};
    if (candidate.extraTargetKnown) value.insert("extra_target_count", candidate.extraTargetCount);
    if (candidate.affectedTargetsKnown)
        value.insert("affected_targets", QJsonArray::fromStringList(candidate.affectedTargets));
    return value;
}

static QJsonObject legacyKeyParts(const Card *card)
{
    // All fixed-baseline virtual string implementations are Card, SkillCard,
    // LuaSkillCard and DummyCard. Round-trip the native output before issuing a
    // cost-substitution template; no authority identity is derived from it.
    // Only parameterized tickets whose producer declares hasIndependentAIConversion
    // can consume this template; that contract excludes suit/number inheritance.
    if (!card || !card->isVirtualCard()) return {};
    const QString key = card->toString(), costs = card->subcardString();
    QString prefix, suffix;
    if (card->isKindOf("DummyCard")) prefix = "$";
    else if (card->isKindOf("LuaSkillCard")) {
        QString name = card->objectName();
        if (!name.startsWith('#')) name.prepend('#');
        prefix = name + "[" + card->getSuitString() + ":" + card->getNumberString() + "]:";
        const auto *skillCard = dynamic_cast<const SkillCard *>(card);
        if (!skillCard) return {};
        suffix = ":" + skillCard->getUserString();
    } else if (const auto *skillCard = dynamic_cast<const SkillCard *>(card)) {
        prefix = "@" + QString::fromLatin1(card->metaObject()->className()) + "[" + card->getSuitString()
            + ":" + card->getNumberString() + "]=";
        if (!skillCard->getUserString().isEmpty()) suffix = ":" + skillCard->getUserString();
    } else prefix = card->objectName() + ":" + card->getSkillName(false) + "[" + card->getSuitString()
        + ":" + card->getNumberString() + "]=";
    if (prefix + costs + suffix != key) return {};
    const QString sorted = legacySortKey(card);
    if (!sorted.startsWith(key)) return {};
    return {{"prefix", prefix}, {"suffix", suffix}, {"sort_suffix", sorted.mid(key.size())}};
}

static QJsonObject responseOwnerRules(Room &room, const ServerPlayer *player, const Card *card)
{
    QJsonObject limits, prohibited;
    for (int method = int(Card::MethodNone); method <= int(Card::MethodGet); ++method)
        limits.insert(QString::number(method), player->isCardLimited(card, Card::HandlingMethod(method)));
    for (const ServerPlayer *other : room.getAlivePlayers())
        prohibited.insert(other->objectName(), player->isProhibited(other, card));
    QJsonObject canUse;
    const auto canUseTargets = projectAICanUseTargets(room, const_cast<ServerPlayer *>(player), card);
    for (auto it = canUseTargets.cbegin(); it != canUseTargets.cend(); ++it) canUse.insert(it.key(), it.value());
    QJsonObject rules{{"limits", limits}, {"prohibited", prohibited}, {"available", card->isAvailable(player)},
        {"can_use", canUse}, {"legacy_key_parts", legacyKeyParts(card)},
        {"no_response", QJsonArray::fromStringList(card->getTag("NoResponse").toStringList())},
        {"target_mod_distance", Sanguosha->correctCardTarget(TargetModSkill::DistanceLimit, player, card)},
        {"target_mod_extra", Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, player, card)},
        {"target_mod_residue", Sanguosha->correctCardTarget(TargetModSkill::Residue, player, card)}};
    if (card->isKindOf("Analeptic")) rules.insert("analeptic_static_available", Analeptic::IsAvailable(player, card));
    // Shape belongs to the authority-produced card; no new conversion permission.
    if (const auto *equip = dynamic_cast<const EquipCard *>(card->getRealCard())) {
        rules.insert("equip_slot", int(equip->location()));
        QJsonArray locations;
        for (int slot : equip->getOccupyLocations()) locations.append(slot);
        rules.insert("equip_occupy_locations", locations);
    }
    return rules;
}

static QMap<QString, int> responseResidue(Room &room, const ServerPlayer *player, const Card *card)
{
    QMap<QString, int> result;
    if ((card->targetFixed() && card->getTypeId() < Card::TypeTrick) || card->isDamageCard()) {
        if (card->targetFixed()) result.insert(player->objectName(),
            Sanguosha->correctCardTarget(TargetModSkill::Residue, player, card, player));
        else for (const ServerPlayer *target : room.getAlivePlayers())
            result.insert(target->objectName(), Sanguosha->correctCardTarget(TargetModSkill::Residue, player, card, target));
    }
    return result;
}

static QJsonObject responseResidueJson(const QMap<QString, int> &values)
{
    QJsonObject result;
    for (auto it = values.cbegin(); it != values.cend(); ++it) result.insert(it.key(), it.value());
    return result;
}

static QJsonObject responseTicketJson(const AICardConversionView &ticket)
{
    AiSkillActionContext identity;
    identity.activationRef = ticket.activationRef; identity.sourceRef = ticket.sourceRef;
    identity.activationQuotaAvailable = ticket.activationQuotaAvailable;
    identity.sourceQuotaAvailable = ticket.sourceQuotaAvailable;
    QJsonObject value = responseIdentity(identity);
    value.insert("conversion_id", ticket.conversionId);
    value.insert("name", ticket.name); value.insert("class_name", ticket.className);
    value.insert("kind_of_names", QJsonArray::fromStringList(ticket.kindOfNames));
    value.insert("suit", ticket.suit); value.insert("number", ticket.number);
    value.insert("skill_name", ticket.skillName); value.insert("type_id", ticket.typeId);
    if (ticket.cardRules.contains("skill_names")) value.insert("skill_names", ticket.cardRules.value("skill_names"));
    for (const QString &key : {QStringLiteral("known_flags"), QStringLiteral("flags_complete"), QStringLiteral("skill_names_complete"), QStringLiteral("equip_slot"), QStringLiteral("equip_occupy_locations")})
        if (ticket.cardRules.contains(key)) value.insert(key, ticket.cardRules.value(key));
    value.insert("virtual_card", true); value.insert("red", ticket.red); value.insert("black", ticket.black);
    value.insert("using", ticket.usingCard); value.insert("locked", ticket.locked);
    value.insert("can_recast", ticket.canRecast); value.insert("will_throw", ticket.willThrow);
    value.insert("damage_card", ticket.damageCard);
    value.insert("single_target", ticket.singleTarget);
    value.insert("legacy_identity_token", ticket.legacyIdentityToken);
    if (ticket.nameLength >= 0) value.insert("name_length", ticket.nameLength);
    value.insert("legacy_string_key", ticket.legacyStringKey);
    value.insert("legacy_sort_key", ticket.legacySortKey);
    value.insert("contains_jinchantuoqiao", ticket.containsJinchantuoqiao);
    value.insert("matches_response_pattern", ticket.matchesResponsePattern);
    value.insert("cost_count", ticket.costCount); value.insert("subcards", responseIds(ticket.subcardIds));
    value.insert("eligible_subcards", responseIds(ticket.eligibleSubcardIds));
    // Lazy resolutions carry the same immutable context as eager tickets so
    // exact same-request reuse cannot confuse play and response conversions.
    value.insert("producer_version", ticket.producerVersion);
    value.insert("producer_reason", int(ticket.producerReason));
    value.insert("producer_pattern", ticket.producerPattern);
    value.insert("policy_class", ticket.policyClass);
    value.insert("sorting_only", ticket.sortingOnly);
    value.insert("declared_name", ticket.declaredName);
    value.insert("target_residue", responseResidueJson(ticket.targetResidue));
    value.insert("evaluation_rules", ticket.cardRules);
    value.insert("handling_method", ticket.handlingMethod);
    if (!ticket.responsePlanning.isEmpty()) value.insert("response_planning", ticket.responsePlanning);
    if (!ticket.responseClassPlanning.isEmpty()) value.insert("response_class_planning", ticket.responseClassPlanning);
    AICardCandidateView candidate;
    candidate.available = ticket.available;
    candidate.targetFixed = ticket.targetFixed; candidate.feasibleWithNoTarget = ticket.feasibleWithNoTarget;
    candidate.completeCoverage = ticket.completeCoverage; candidate.legalTargets = ticket.legalTargets;
    candidate.targetCombinations = ticket.targetCombinations; candidate.maxVotes = ticket.maxVotes;
    candidate.targetFilterEmpty = ticket.targetFilterEmpty;
    const auto canUse = ticket.cardRules.value("can_use").toObject();
    for (auto it = canUse.begin(); it != canUse.end(); ++it) candidate.canUseTargets.insert(it.key(), it.value().toBool());
    candidate.extraTargetKnown = ticket.extraTargetKnown; candidate.extraTargetCount = ticket.extraTargetCount;
    candidate.extraTargetByPlayer = ticket.extraTargetByPlayer;
    candidate.affectedTargetsKnown = ticket.affectedTargetsKnown; candidate.affectedTargets = ticket.affectedTargets;
    const QJsonObject targets = responseCandidateJson(candidate);
    for (auto it = targets.begin(); it != targets.end(); ++it) value.insert(it.key(), it.value());
    return value;
}

static QJsonArray nonHintStrategyEffects(const QJsonArray &effects)
{
    QJsonArray values;
    for (const auto &effect : effects)
        if (effect.toObject().value("kind") != "ai_player_hint") values.append(effect);
    return values;
}

static bool commitAcceptedCardEffects(Room &room, const AIRequest &request,
                                      const AIResult &result, const Card *card, QString *error)
{
    if (!AiLuaRuntime::validateDecisionEffects(request.worldView, result.strategyEffects)) return false;
    if (!stageAcceptedAIStrategyCardHints(room, request, result.strategyEffects, card, error, false)) return false;
    if (!room.roomRuntime()->ai().commitStrategyEffects(request.worldView,
        nonHintStrategyEffects(result.strategyEffects), room.roomRuntime()->lua(), error)) return false;
    return stageAcceptedAIStrategyCardHints(room, request, result.strategyEffects, card, error);
}

static bool commitAcceptedChoiceEffects(Room &room, const AIRequest &request, const AIResult &result)
{
    if (result.strategyEffects.isEmpty()) return true;
    QString error;
    if (!AiLuaRuntime::validateDecisionEffects(request.worldView, result.strategyEffects)
        || !stageAIRound26ChoiceFlags(room, request, result, false, &error)) return false;
    QJsonArray ordinary;
    for (const auto &effect : result.strategyEffects)
        if (effect.toObject().value("kind") != "srkill_choice_flag") ordinary.append(effect);
    if (!room.roomRuntime()->ai().commitStrategyEffects(request.worldView,
        ordinary, room.roomRuntime()->lua(), &error)) return false;
    return stageAIRound26ChoiceFlags(room, request, result, true, &error);
}

// Exact source questions which may save an aiUseCard result across requests.
static bool needsOlClanObservationCustody(const AIRequest &request, const ServerPlayer *player)
{
    return player && ((request.kind==AIRequest::AmazingGrace && request.choiceOptions.reason=="kezufuxun" && player->hasSkill("kezufuxun"))
        || (request.kind==AIRequest::Activate && request.reason==CardUseStruct::CARD_USE_REASON_PLAY && request.pattern.isEmpty() && player->hasSkill("zuchengqi")));
}

static const AICardConversionView *responseTicket(const AIRequest &request, int id)
{
    for (const AICardConversionView &ticket : request.cardConversions)
        if (ticket.conversionId == id) return &ticket;
    if (request.conversionLedger)
        for (const AICardConversionView &ticket : request.conversionLedger->tickets)
            if (ticket.conversionId == id) return &ticket;
    return nullptr;
}

static bool usesClassResponsePlanning(const AIRequest &request)
{
    if (request.kind != AIRequest::UseCard) return false;
    const auto dispatch = request.choiceOptions.context.value("use_card_dispatch").toObject();
    if (!dispatch.value("known").toBool()) return false;
    const auto raw = dispatch.value("raw").toObject();
    if (raw.value("known").toBool() && raw.value("kind") == "class") return true;
    if (dispatch.value("entry_variant") == "ordinary"
        && raw.value("known").toBool()
        && (!raw.value("present").toBool() || raw.value("kind") == "nonfunction")) {
        const auto prompt = dispatch.value("prompt").toObject();
        return prompt.value("known").toBool() && prompt.value("kind") == "class";
    }
    return false;
}

static QJsonObject publicDecisionCard(const Card *card, bool ownerEvaluation = false)
{
    if (!card) return {};
    // A declared/judgement card is public; its backing IDs and subcards are not
    // needed by a policy and must never expose a hidden payment card.
    QJsonObject result{{"name", card->objectName()}, {"name_length", card->nameLength()}, {"class_name", card->getClassName()},
        {"kind_of", QJsonArray::fromStringList(card->getKindOfNames())},
        {"type_id", int(card->getTypeId())}, {"suit", int(card->getSuit())},
        {"subtype", card->getSubtype()},
        {"single_target", card->isSingleTargetCard()}, {"damage_card", card->isDamageCard()}, {"will_throw", card->willThrow()},
        {"skill_name", card->getSkillName(false)},
        {"number", card->getNumber()}, {"red", card->isRed()}, {"black", card->isBlack()}};
    if (const auto *equip = dynamic_cast<const EquipCard *>(card->getRealCard())) {
        result.insert("equip_slot", int(equip->location()));
        QJsonArray locations;
        for (int slot : equip->getOccupyLocations()) locations.append(slot);
        result.insert("equip_occupy_locations", locations);
    }
    result.insert("skill_names_complete", ownerEvaluation);
    if (ownerEvaluation) {
        QJsonArray names;
        for (const QString &name : card->getSkillNames()) names.append(name);
        result.insert("skill_names", names);
    }
    const QString identity = aiScopedLegacyCardIdentity(card);
    if (!identity.isEmpty()) result.insert("legacy_identity_token", identity);
    result.insert("log_name", card->getLogName());
    if (!card->isVirtualCard()) {
        result.insert("id", card->getId());
        result.insert("effective_id", card->getEffectiveId());
    } else if (card->getSubcards().isEmpty()) {
        result.insert("effective_id", -1);
        result.insert("no_cost_virtual", true);
    }
    // A borrowed activation can have a private root skill. The public action
    // establishes the activation name only; do not reveal a different root.
    if (card->getSourceSkillName() == card->getSkillName(false))
        result.insert("source_skill", card->getSourceSkillName());
    if (const auto *weapon = dynamic_cast<const Weapon *>(card->getRealCard()))
        result.insert("weapon_range", weapon->getRange());
    // A public face does not publish private card flags. In particular,
    // viewer-specific visibility flags survive moves into public locations.
    // Only an explicitly owner-authorized descriptor has complete flag state.
    if (ownerEvaluation) {
        QJsonObject flags;
        for (const QString &flag : card->getFlags())
            if (!flag.startsWith("visible_")) flags.insert(flag, true);
        result.insert("known_flags", flags);
        result.insert("flags_complete", true);
        result.insert("flags_complete_scope", "owner_non_visibility");
    } else {
        const auto observed = readAIStrategyScopedPublicCardFlags(card);
        if (!observed.isEmpty()) result.insert("known_flags", observed);
    }
    return result;
}

// Only call for a freshly constructed, disclosure-checked Lihuo hypothesis or
// its native observer provenance. No public/foreign CardUse inherits this path.
static QJsonObject lihuoPolicyCard(const Card *card)
{
    auto face=publicDecisionCard(card,true);
    face.insert("id",card->getId());face.insert("effective_id",card->getEffectiveId());
    face.insert("virtual_card",card->isVirtualCard());face.insert("subcards",responseIds(card->getSubcards()));
    face.insert("target_fixed",card->targetFixed());face.insert("handling_method",int(card->getHandlingMethod()));
    face.insert("can_recast",card->canRecast());
    face.insert("legacy_string_key",card->toString());face.insert("legacy_sort_key",legacySortKey(card));
    return face;
}

}

AICardCandidateView projectAIStrategyCandidate(Room &room, ServerPlayer *player, const Card *card,
    Card::HandlingMethod method, AIRequest::DecisionKind kind, const QString &pattern, int &budget)
{
    return makeAICardCandidate(room, player, card, method, kind, pattern, budget);
}

AiDecisionCoordinator::AiDecisionCoordinator(Room &room,
                                             SkillRuntimeCoordinator &skillRuntime)
    : m_room(room), m_skillRuntime(skillRuntime)
{
    m_room.roomRuntime()->ai().setConversionResolver([this](const AIRequest &request, const QJsonObject &proposal) {
        return resolveConversionProposal(request, proposal);
    });
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
    Q_UNUSED(value);
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
    if (triggerEvent == GameReady) { m_effectKnown = true; m_lastEffect = {}; m_lastFilterCardEffect = {}; m_lastTargetUse = {}; }
    if (triggerEvent == RoundStart && target && target == m_room.getCurrent()
        && data.userType() == QMetaType::Int)
        m_turnCount = data.toInt();
    // One bounded, value-only log per Room. Nothing here keeps a pointer: the structs
    // are read once, while they are still valid, and only names and ids are kept.
    if (target) resetAIRound30TargetCache(m_room);
    AIEventView event;
    event.sequence = ++m_eventSequence;
    event.revision = m_room.roomRuntime()->stateRevision();
    event.triggerEvent = triggerEvent;
    event.kind = QStringLiteral("other");
    event.details.insert("unix_time", double(QDateTime::currentSecsSinceEpoch()));
    event.details.insert("turn_count", m_turnCount);
    if (target) {
        event.to = target->objectName();
        event.details.insert("player", target->objectName());
    }
    const auto describeCard = [&event](const Card *card) {
        if (!card) return;
        event.cardClass = card->getClassName();
        event.cardSkill = card->getSkillName(false);
        event.details.insert("card", publicDecisionCard(card));
        QJsonArray publicSkills;
        if (!event.cardSkill.isEmpty()) publicSkills.append(event.cardSkill);
        event.details.insert("card_skills", publicSkills);
        event.details.insert("card_skills_complete", false);
        // Private strategy flags are projected only for the card actor below
        // the viewer boundary, never stored in the shared event descriptor.
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
            || move.to_place == Player::DiscardPile;
        if (publicDestination) {
            event.cardIds = move.card_ids;
        } else if (move.to_place == Player::PlaceHand || move.to_place == Player::PlaceSpecial) {
            event.privateCardIds = move.card_ids;
            event.privateViewer = event.to;
        }
    } else if (data.canConvert<CardEffectStruct>()) {
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        if (triggerEvent == CardEffect) {
            m_effectKnown = true;
            m_lastFilterCardEffect = QJsonObject{{"from", effect.from ? effect.from->objectName() : QString()},
                {"to", effect.to ? effect.to->objectName() : QString()}, {"offset_num", effect.offset_num},
                {"card", publicDecisionCard(effect.card)}};
            m_lastEffect = QJsonObject{{"from", effect.from ? effect.from->objectName() : QString()},
                {"to", effect.to ? effect.to->objectName() : QString()}};
            if (effect.card) {
                Card *printed = Sanguosha->cloneCard(effect.card->objectName(), effect.card->getSuit(), effect.card->getNumber());
                if (printed) { m_lastEffect.insert("card", publicDecisionCard(printed)); printed->deleteLater(); }
            }
        }
        event.details.insert("offset_num", effect.offset_num);
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
            event.good = judge->isGood();
            event.details.insert("judge", QJsonObject{{"pattern", judge->pattern},
                {"pattern_good", judge->good}, {"good", judge->isGood()}, {"reason", judge->reason},
                {"who", event.to}, {"card", publicDecisionCard(judge->card)}});
            QJsonObject judgeDto = event.details.value("judge").toObject();
            std::unique_ptr<Card> reasonCard(Sanguosha->cloneCard(judge->reason));
            judgeDto.insert("reason_card", reasonCard ? QJsonValue(publicDecisionCard(reasonCard.get())) : QJsonValue(false));
            event.details.insert("judge", judgeDto);
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
        event.details.insert("damage_present", death.damage != nullptr);
        if (death.damage) {
            event.chain = death.damage->chain;
            event.transfer = death.damage->transfer;
            event.from = death.damage->from ? death.damage->from->objectName() : QString();
            event.reason = death.damage->reason;
            if (death.damage->card) event.cardName = death.damage->card->objectName();
            describeCard(death.damage->card);
        }
    } else if (data.canConvert<DrawStruct>()) {
        const DrawStruct draw = data.value<DrawStruct>();
        event.kind = QStringLiteral("draw");
        event.details.insert("draw", QJsonObject{{"reason", draw.reason}});
    } else if (data.canConvert<DyingStruct>()) {
        const DyingStruct dying = data.value<DyingStruct>();
        event.kind = QStringLiteral("dying");
        event.to = dying.who ? dying.who->objectName() : QString();
    } else if (data.canConvert<CardUseStruct>()) {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (triggerEvent == CardUsed && use.card && use.from) {
            QStringList targets;
            for (const auto *player : use.to) if (player) targets.append(player->objectName());
            witnessActualAIStrategyCardHints(m_room, use.card, use.from, targets);
        }
        event.kind = QStringLiteral("card_use");
        event.from = use.from ? use.from->objectName() : QString();
        if (use.whocard) event.details.insert("responded_card_class", use.whocard->getClassName());
        if (triggerEvent == PreCardUsed) {
            m_previousUseCard = const_cast<Card *>(use.card);
            m_previousUseFrom = use.from;
        }
        event.details.insert("use_matches_previous", m_previousUseCard && m_previousUseCard == use.card
            && m_previousUseFrom == target);
        QJsonObject targetFacts;
        for (ServerPlayer *to : use.to) {
            if (!use.from || !to) continue;
            targetFacts.insert(to->objectName(), QJsonObject{{"distance_from", use.from->distanceTo(to)},
                {"in_from_attack_range", use.from->inMyAttackRange(to)}});
        }
        event.details.insert("target_facts", targetFacts);
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
            if (kind == QStringLiteral("Yiji"))
                event.details.insert("card_count", choice.value(3).split(QChar('+'), Qt::SkipEmptyParts).size());
            for (const QString &name : choice.value(2).split(QChar('+'), Qt::SkipEmptyParts)) {
                if (m_room.findPlayerByObjectName(name, true)) event.targets << name;
            }
        } else if (kind == QStringLiteral("peach") && choice.size() >= 4
                   && target && m_room.findPlayerByObjectName(choice.value(1), true)) {
            // Selected response precedes public use. Only the answering viewer
            // observes this event; never disclose the serialized selected card.
            event.kind = QStringLiteral("choice");
            event.from = target->objectName();
            event.privateEvent = true; event.privateViewer = event.from;
            event.reason = choice.value(1);
            event.targets << choice.value(1);
            event.details.insert("choice_kind", kind);
        } else if (kind == QStringLiteral("Nullification") || kind == QStringLiteral("cardResponded")
                   || kind == QStringLiteral("cardUsed") || kind == QStringLiteral("cardChosen")
                   || kind == QStringLiteral("viewCards")) {
            event.kind = QStringLiteral("choice");
            event.from = target ? target->objectName() : QString();
            event.privateEvent = true;
            event.privateViewer = event.from;
            event.reason = choice.value(1);
            event.details.insert("choice_kind", kind);
            event.details.insert("prompt_list", QJsonArray::fromStringList(choice));
        } else if (kind == QStringLiteral("skillInvoke") || kind == QStringLiteral("skillChoice")) {
            event.kind = QStringLiteral("choice");
            event.from = target ? target->objectName() : QString();
            // This internal notification does not prove the answer was broadcast.
            event.privateEvent = true;
            event.privateViewer = event.from;
            event.reason = choice.value(1);
            event.details.insert("choice_kind", kind);
            // Keep the source positional ABI. Target-bearing skillInvoke has
            // four fields; its answer is the last field, not the target name.
            // This stays restricted to the answering actor above.
            event.details.insert("prompt_list", QJsonArray::fromStringList(choice));
            event.details.insert("answer", choice.isEmpty() ? QString() : choice.last());
        }
    }
    if (triggerEvent == TargetSpecified && event.kind == QStringLiteral("card_use"))
        m_lastTargetUse = QJsonObject{{"from", event.from}, {"to", QJsonArray::fromStringList(event.targets)},
            {"card", event.details.value("card")}};
    recordAIRound26PlanningPublicUse(m_room, triggerEvent, data);
    while (m_events.size() >= AiEventLogLimit)
        m_events.removeFirst();
    m_events << event;

    // Consume at the event boundary, never by replaying the truncated diagnostic
    // log. One canonical stage per action prevents duplicate intention updates.
    const bool canonical = triggerEvent == GameStart || triggerEvent == CardFinished
        || triggerEvent == Dying || triggerEvent == DamageForseen || triggerEvent == EventPhaseStart
        || triggerEvent == EventPhaseEnd
        || triggerEvent == DrawNCards || triggerEvent == AfterDrawNCards || triggerEvent == TurnStart || triggerEvent == FinishJudge || triggerEvent == CardOffset
        || triggerEvent == Damaged || triggerEvent == GameReady || triggerEvent == RoundStart
        || triggerEvent == PreCardUsed || triggerEvent == CardEffect
        || triggerEvent == StartJudge || triggerEvent == AskForRetrial
        || triggerEvent == TargetSpecified || triggerEvent == TargetConfirmed || triggerEvent == DamageInflicted
        || triggerEvent == HpRecover || triggerEvent == Death || triggerEvent == BeforeGameOverJudge
        || triggerEvent == Revived || triggerEvent == CardsMoveOneTime
        || triggerEvent == ShowCards || triggerEvent == EventPhaseChanging
        || (triggerEvent == ChoiceMade && event.kind == QStringLiteral("choice"));
    if (!canonical || !Config.EnableAI || !m_room.roomRuntime()->ai().lua().rawState()) return;
    // Share the same revision-scoped public facts with decision requests.
    for (ServerPlayer *observer : m_room.getAlivePlayers()) {
        if (event.privateEvent && event.privateViewer != observer->objectName()) continue;
        AIWorldView world = buildWorldView(observer, true, true);
        AIEventView visible = event;
        // The AOE observer compares the actual public card across nested events.
        // Issue its equality label only inside this viewer's boundary; the shared
        // Room event never carries another viewer's identity namespace.
        if ((triggerEvent == PreCardUsed || triggerEvent == TargetConfirmed || triggerEvent == CardFinished)
            && visible.kind == "card_use" && data.canConvert<CardUseStruct>()) {
            if (const Card *card = data.value<CardUseStruct>().card) {
                QJsonObject face = visible.details.value("card").toObject();
                face.insert("legacy_identity_token", aiLegacyCardIdentity(m_room, observer, card));
                visible.details.insert("card", face);
            }
        }
        if (triggerEvent == ChoiceMade) {
            world.strategyFacts.insert("kaiyuan_role_witness", false);
            if (event.details.value("choice_kind") == "peach" && event.targets.size() == 1) {
                QJsonArray sources;
                bool complete = true;
                for (auto *source : m_room.findPlayersBySkillName("KaiyuanShengshi")) {
                    const auto flag = readAIStrategyPublicKaiyuanFlag(m_room, source);
                    if (!flag.isBool()) { complete = false; break; }
                    if (flag.toBool())
                        sources.append(source->objectName());
                }
                if (complete) world.strategyFacts.insert("kaiyuan_role_witness", QJsonObject{
                    {"player", event.targets.first()}, {"sources", sources}});
                else world.strategyFacts.remove("kaiyuan_role_witness");
            }
        }
        // Quanxiang owns a distinct event-scoped witness. No native role or
        // hidden skill membership is exposed by this strategy-state authority.
        world.strategyFacts.insert("quanxiang_role_witness", false);
        if (triggerEvent == Death && !event.to.isEmpty()) {
            QJsonArray sources;
            bool complete = true;
            for (ServerPlayer *source : m_room.getOtherPlayers(target)) {
                const bool skillsKnown = source == observer || !ServerInfo.EnableHegemony
                    || source->hasShownAllGenerals() || source->isDead();
                if (!skillsKnown) { complete = false; break; }
                if (source->hasSkill("luaRquanxiang")) sources.append(source->objectName());
            }
            if (complete) world.strategyFacts.insert("quanxiang_role_witness", QJsonObject{
                {"player", event.to}, {"sources", sources}});
            else world.strategyFacts.remove("quanxiang_role_witness");
        } else if (triggerEvent == ChoiceMade && event.reason == "luaRquanxiang"
            && event.details.value("choice_kind") == "skillInvoke"
            && event.details.value("answer") == "yes" && event.from == observer->objectName()) {
            const QVariant value = m_room.getTag("CurrentDamageStruct");
            if (value.canConvert<DamageStruct>()) {
                const auto damage = value.value<DamageStruct>();
                if (damage.to) world.strategyFacts.insert("quanxiang_role_witness", QJsonObject{
                    {"player", damage.to->objectName()}, {"sources", QJsonArray{event.from}}});
            }
        }
        // These four donor callbacks only change observer-local role estimates.
        // Authorize exact public participants at the actual source event; never
        // project their true roles or reuse another skill's role witness.
        QJsonObject roleRows;
        const bool initialDraw = triggerEvent == AfterDrawNCards
            && data.canConvert<DrawStruct>() && data.value<DrawStruct>().reason == "InitialHandCards";
        if (target && (triggerEvent == GameStart || triggerEvent == Death || triggerEvent == TurnStart || initialDraw)) {
            QJsonArray mercenary, silent, love, foreverTargets;
            for (ServerPlayer *subject : m_room.getOtherPlayers(target)) {
                const bool skillsKnown = subject == observer || !ServerInfo.EnableHegemony
                    || subject->hasShownAllGenerals() || subject->isDead();
                if (skillsKnown && subject->hasSkill("Cjiyongbing") && !initialDraw)
                    mercenary.append(subject->objectName());
                if (skillsKnown && subject->hasSkill("chenmohy") && triggerEvent == GameStart)
                    silent.append(subject->objectName());
                if (initialDraw) {
                    if (skillsKnown && subject->hasSkill("lovesong") && subject->getMark("lovesong") > 0)
                        love.append(subject->objectName());
                    if (subject->getMark("yuriforever") > 0) foreverTargets.append(subject->objectName());
                }
            }
            if (!initialDraw) roleRows.insert("Cjiyongbing", mercenary);
            if (triggerEvent == GameStart) roleRows.insert("chenmohy", silent);
            if (initialDraw) { roleRows.insert("lovesong", love); roleRows.insert("yuriforever", foreverTargets); }
        }
        world.strategyFacts.insert("role_row_update_witness", roleRows);
        world.strategyFacts.insert("sfofl_longmu_role_witness", false);
        if (triggerEvent == BeforeGameOverJudge && data.canConvert<DeathStruct>()) {
            const auto death = data.value<DeathStruct>();
            auto *victim = death.who;
            const bool generalKnown = victim && (victim == observer || !ServerInfo.EnableHegemony
                || victim->hasShownAllGenerals() || victim->isDead());
            if (generalKnown && !victim->getGeneralName().contains("zombie")
                && !victim->getGeneral2Name().contains("zombie")) {
                QJsonArray sources;bool complete=true;
                for (auto *source : m_room.getOtherPlayers(target)) {
                    const bool skillsKnown = source == observer || !ServerInfo.EnableHegemony
                        || source->hasShownAllGenerals() || source->isDead();
                    if (!skillsKnown) { complete=false;break; }
                    if (source->hasSkill("sfofl_longmu") && victim->getMark("sfofl_longmu" + source->objectName()) > 0)
                        sources.append(source->objectName());
                }
                if (complete) world.strategyFacts.insert("sfofl_longmu_role_witness",QJsonObject{
                    {"player", victim->objectName()}, {"sources", sources}});
                else world.strategyFacts.remove("sfofl_longmu_role_witness");
            } else if (!generalKnown) world.strategyFacts.remove("sfofl_longmu_role_witness");
        }
        world.strategyFacts.insert("du_tongque_role_witness", false);
        if (triggerEvent == ChoiceMade && event.from == observer->objectName()) {
            const auto fields = event.details.value("prompt_list").toArray();
            if (fields.size() >= 4 && fields.at(2) == "#caocao_tongque") {
                const auto *dying = m_room.findPlayerByObjectName(fields.at(3).toString());
                if (dying) world.strategyFacts.insert("du_tongque_role_witness", QJsonObject{
                    {"player", dying->objectName()}});
            }
        }
        // Resolve attribution only through skills the observer is entitled to know.
        const QString ownerSkill = !event.reason.isEmpty() ? event.reason : event.cardSkill;
        if (event.kind == QStringLiteral("damage") || event.kind == QStringLiteral("judge")) {
            bool complete = true;
            QString owner;
            for (ServerPlayer *candidate : m_room.getAllPlayers(true)) {
                bool skillsComplete = candidate == observer || !ServerInfo.EnableHegemony
                    || candidate->hasShownAllGenerals() || candidate->isDead();
                if (!skillsComplete) { complete = false; break; }
                if (!ownerSkill.isEmpty() && candidate->hasSkill(ownerSkill)) {
                    owner = candidate->objectName(); break;
                }
            }
            visible.details.insert("intention_from_complete", complete);
            if (complete) visible.details.insert("intention_from", owner);
        }
        if (visible.privateViewer != observer->objectName()) visible.privateCardIds.clear();
        // Verify the recipient before removing private native routing metadata.
        projectAIStrategyScarletEventContext(world, m_room, observer, visible,
            [](const Card *card) { return publicDecisionCard(card); });
        projectAIStrategyEvent(visible, world, m_room, observer, target, data);
        projectAIRound50HTMSIntention(world, m_room, observer, visible,
            [](const Card *card) { return publicDecisionCard(card); });
        projectAIRound41SkillFlagEvent(world, m_room, observer, visible);
        projectAIRound45NewgeneralsEvent(world,m_room,observer,visible);
        projectAIRound50YuriEvent(world,m_room,observer,target,visible,data,[](const Card*card){return publicDecisionCard(card);});
        projectAIRound31IntentionEvent(visible, world, m_room, observer, target, data,
            [](const Card *card) { return publicDecisionCard(card); });
        recordAIRound27OriginalEvent(world, m_room, observer, target, visible, data);
        projectAIStrategyRound26ResponseEvent(world, m_room, observer, visible);
        projectAIStrategyResponseEventContext(world, m_room, observer, visible,
            [](const Card *card) { return publicDecisionCard(card); });
        projectAIStrategyRetrialEventContext(world, m_room, observer, visible,
            [](const Card *card) { return publicDecisionCard(card); });
        projectAIRound25ImagineEvent(world, m_room, observer, visible);
        visible.privateViewer.clear();
        projectAIStrategyRound21OriginalEvent(world, m_room, observer, visible);
        projectAIStrategyDragonEvent(world, visible);
        projectAIRound40AzirEvent(world, m_room, observer, target, visible, data);
        projectAIRound47DapoEvent(world, m_room, observer, target, visible, data);
        QList<AICardView> disclosed;
        for (const QJsonValue &value : visible.details.value("move").toObject().value("cards").toArray()) {
            const QJsonObject card = value.toObject().value("card").toObject();
            if (!card.value("id").isDouble() || card.value("id").toInt(-1) < 0) continue;
            AICardView view;
            view.cardId = view.effectiveId = card.value("id").toInt();
            view.objectName = card.value("name").toString();
            view.nameLength = card.value("name_length").toInt(-1);
            view.className = card.value("class_name").toString();
            view.suit = card.value("suit").toInt(int(Card::NoSuit));
            view.number = card.value("number").toInt();
            view.typeId = card.value("type_id").toInt();
            view.skillName = card.value("skill_name").toString();
            view.red = card.value("red").toBool(); view.black = card.value("black").toBool();
            view.knownFlags = card.value("known_flags").toObject();
            view.flagsComplete = card.value("flags_complete").toBool();
            view.skillNamesComplete = card.value("skill_names_complete").toBool();
            if (view.skillNamesComplete) for (const auto &name : card.value("skill_names").toArray())
                if (name.isString()) view.skillNames.append(name.toString());
            for (const auto &kind : card.value("kind_of").toArray())
                if (kind.isString()) view.kindOfNames << kind.toString();
            disclosed << view;
        }
        projectAIStrategyDisclosedFilters(world, m_room, observer, disclosed);
        projectAIStrategyKnownCards(world, m_room, observer);
        observeAIStrategyHistory(world, m_room, visible);
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
    const quint64 weaponRangeRevision = Weapon::rangeRevision();
    const auto players = m_room.getPlayers();
    const auto alivePlayers = m_room.getAlivePlayers();
    if (m_publicBoardValid && weaponRangeRevision != 0
        && m_publicBoardWeaponRangeRevision == weaponRangeRevision
        && m_publicBoard.revision == revision
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
    m_publicBoardValid = Weapon::rangeRevisionCurrent(weaponRangeRevision)
        && revision == m_room.roomRuntime()->stateRevision()
        && generation == SkillSet::generation()
        && definitions == m_room.roomRuntime()->definitions().skillDefinitionVersion()
        && players == m_room.getPlayers()
        && alivePlayers == m_room.getAlivePlayers();
    if (m_publicBoardValid) {
        m_publicBoard = board;
        m_publicBoardGeneration = generation;
        m_publicBoardDefinitions = definitions;
        m_publicBoardWeaponRangeRevision = weaponRangeRevision;
        m_publicBoardPlayers = players;
        m_publicBoardAlivePlayers = alivePlayers;
    }
    if (probe) qInfo() << "[AI_WV] public_board build revision=" << revision
                       << "event=" << eventSequence << "ms=" << timer.elapsed()
                       << "reusable=" << m_publicBoardValid;
    return board;
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
        playerView.kongcheng = player->isKongcheng();
        playerView.wounded = player->isWounded();
        playerView.faceUp = player->faceUp();
        playerView.chained = player->isChained();
        {
            // Preserve the existing ordinary full-world caller's own proof.
            // The new exact consumer is for the previously unscoped path.
            std::unique_ptr<AIRound56MaxCardsScope> maxCardsScope;
            auto *gameLua=m_room.roomRuntime()->lua().rawState();
            if(!inAIRound56ProjectionScope(m_room,gameLua))
                maxCardsScope.reset(new AIRound56MaxCardsScope(m_room,player,gameLua));
            playerView.maxCards = player->getMaxCards();
        }
        playerView.hujia = player->getHujia();
        {
            std::unique_ptr<AIRound56AttackRangeScope> attackRangeScope;
            auto *gameLua=m_room.roomRuntime()->lua().rawState();
            if(!inAIRound56ProjectionScope(m_room,gameLua))
                attackRangeScope.reset(new AIRound56AttackRangeScope(m_room,player,gameLua));
            playerView.attackRange = player->getAttackRange();
            playerView.attackRangeWithoutWeapon = player->getAttackRange(false);
        }
        playerView.gender = int(player->getGender());
        playerView.lord = player->isLord();
        // Armor visibility still depends on the viewer in hegemony. The blocking
        // scan and the physical armor do not, so they are resolved once.
        bool armorBlocked = false;
        {
            // Exact original hasSkill/isSkillInvalid source union, established
            // before choosing any ViewAsEquip holder or instance branch.
            std::unique_ptr<AIRound56PublicSkillQueriesScope> skillSource;
            auto *gameLua=m_room.roomRuntime()->lua().rawState();
            if(!inAIRound56ProjectionScope(m_room,gameLua))
                skillSource.reset(new AIRound56PublicSkillQueriesScope(m_room,player,gameLua));
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

        {
            // Separate visible-definition/validity lifetime; excludes physical
            // armor, cards, piles and every later request consumer.
            std::unique_ptr<AIRound56PublicSkillQueriesScope> skillSource;
            auto *gameLua=m_room.roomRuntime()->lua().rawState();
            if(!inAIRound56ProjectionScope(m_room,gameLua))
                skillSource.reset(new AIRound56PublicSkillQueriesScope(m_room,player,gameLua));
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
                for (const QMetaObject *meta = skill->metaObject(); meta; meta = meta->superClass())
                    skillView.skillClasses << QString::fromLatin1(meta->className());
                skillView.frequency = int(skill->getFrequency(player));
                skillView.description = skill->getDescription();
                if (const auto *trigger = dynamic_cast<const TriggerSkill *>(skill)) {
                    for (TriggerEvent event : trigger->getTriggerEvents()) skillView.triggerEvents << int(event);
                    skillView.retrialTrigger = trigger->getTriggerEvents().contains(AskForRetrial);
                    const QString description = skill->getDescription();
                    skillView.loseEquipDescriptionTrigger = trigger->getTriggerEvents().contains(CardsMoveOneTime)
                        && description.contains(QStringLiteral("装备区")) && description.contains(QStringLiteral("失去"));
                }
                skillView.lordSkill = skill->isLordSkill();
                skillView.attachedLordSkill = skill->isAttachedLordSkill();
                // A lord skill only works while its owner is the lord of a played kingdom.
                skillView.lordSkillEffective = skill->isLordSkill()
                    && player->hasLordSkill(skill->objectName());
                skillView.correctState = makeAIStateObject(instance.correctState);
                row.skills << cached;
            }
        }
        board.players << row;
    }
    return board;
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
    const ServerPlayer *currentDying = m_room.getCurrentDyingPlayer();
    world.strategyFacts.insert("current_dying_player", currentDying ? currentDying->objectName() : QString());
    world.currentPhase = board.currentPhase;
    world.turnCount = m_turnCount;
    world.enableHegemony = ServerInfo.EnableHegemony;
    world.aiHumanized = m_room.getMode() != QStringLiteral("50p")
        && Config.value(QStringLiteral("AIHumanized"), true).toBool();
    world.aiChatEnabled = viewer && viewer->getState() == QStringLiteral("robot")
        && Config.OriginAIDelay > 0 && world.aiHumanized;
    world.duelRule = Config.value(QStringLiteral("1v1/Rule"), QStringLiteral("Classical")).toString();
    world.strategyFacts.insert("draw_pile_count", m_room.getDrawPile().size());
    world.strategyFacts.insert("speech_counters", m_room.roomRuntime()->ai().speechCounters());
    world.responsePolicy.insert("origin_ai_delay", Config.OriginAIDelay);
    world.responsePolicy.insert("humanized", world.aiHumanized);
    world.responsePolicy.insert("turn_count", m_turnCount);
    // Read only personality configuration metadata; no legacy strategy executes.
    lua_State *settingsState = m_room.roomRuntime()->lua().state();
    const int settingsTop = lua_gettop(settingsState);
    lua_getglobal(settingsState, "sgs");
    if (lua_istable(settingsState, -1)) {
        lua_getfield(settingsState, -1, "jl_bingfen");
        world.responsePolicy.insert("jl_bingfen", bool(lua_toboolean(settingsState, -1)));
        lua_pop(settingsState, 1);
        for (const auto &entry : {qMakePair("jl_bingfen1", "bingfen_invoke_line_count"),
                                 qMakePair("jl_bingfen3", "bingfen_peach_line_count")}) {
            lua_getglobal(settingsState, entry.first);
            world.responsePolicy.insert(entry.second, lua_istable(settingsState, -1) ? int(lua_rawlen(settingsState, -1)) : 0);
            const QString prefix = QString::fromLatin1(entry.first) == "jl_bingfen1"
                ? QStringLiteral("bingfen_invoke") : QStringLiteral("bingfen_peach");
            world.responsePolicy.insert(prefix + "_lines_present", bool(lua_toboolean(settingsState, -1)));
            if (lua_istable(settingsState, -1) && lua_rawlen(settingsState, -1) <= 256) {
                QJsonArray lines;
                bool complete = true;
                for (size_t i = 1; i <= lua_rawlen(settingsState, -1); ++i) {
                    lua_rawgeti(settingsState, -1, i);
                    size_t size = 0;
                    const char *text = lua_type(settingsState, -1) == LUA_TSTRING
                        ? lua_tolstring(settingsState, -1, &size) : nullptr;
                    if (!text || size > 16384) complete = false;
                    else lines.append(QString::fromUtf8(text, qsizetype(size)));
                    lua_pop(settingsState, 1);
                }
                if (complete) world.responsePolicy.insert(prefix + "_lines", lines);
            }
            lua_pop(settingsState, 1);
        }
    }
    lua_settop(settingsState, settingsTop);
    if (m_effectKnown) {
        world.decisionContext.insert("effect_present", !m_lastEffect.isEmpty());
        if (!m_lastEffect.isEmpty()) world.decisionContext.insert("effect", m_lastEffect);
        world.decisionContext.insert("filter_card_effect_known", true);
        world.decisionContext.insert("filter_card_effect_present", !m_lastFilterCardEffect.isEmpty());
        if (!m_lastFilterCardEffect.isEmpty()) {
            world.decisionContext.insert("last_filter_card_effect", m_lastFilterCardEffect);
            world.strategyFacts.insert("response_effect_card", m_lastFilterCardEffect.value("card"));
        }
    }
    if (!m_lastTargetUse.isEmpty()) world.decisionContext.insert("last_target_specified_use", m_lastTargetUse);
    // Printed descriptors are evaluation-only, never authorized response/play tickets.
    if (!eventOnly && viewer) {
        for (const QString &name : {"jink", "slash", "peach", "fire_slash", "thunder_slash", "analeptic"}) {
            Card *probe = Sanguosha->cloneCard(name);
            if (!probe) continue;
            QJsonObject row = publicDecisionCard(probe, true);
            row.insert("evaluation_only", true);
            row.insert("virtual_card", true);
            row.insert("effective_id", -1);
            row.insert("id", -1);
            row.insert("subcards", QJsonArray());
            row.insert("can_recast", probe->canRecast());
            world.evaluationCards.insert(name, row);
            probe->deleteLater();
        }
    }
    // Composition is a public mode rule, never a count of unrevealed identities.
    const QMap<QChar, QString> roleNames{{'Z', "lord"}, {'C', "loyalist"}, {'F', "rebel"}, {'N', "renegade"}};
    QJsonObject counts{{"lord", 0}, {"loyalist", 0}, {"rebel", 0}, {"renegade", 0}};
    const QString roles = Sanguosha->getRoles(world.modeId);
    bool countsKnown = !roles.isEmpty() && roles.size() == board.players.size();
    for (const QChar role : roles) {
        if (!roleNames.contains(role)) { countsKnown = false; break; }
        const QString name = roleNames.value(role);
        counts.insert(name, counts.value(name).toInt() + 1);
    }
    for (const PublicPlayer &row : board.players) {
        if (!row.view.dead) continue;
        if (!viewer || !m_room.canSeeRole(viewer, row.player) || !counts.contains(row.role)) {
            countsKnown = false; break;
        }
        counts.insert(row.role, counts.value(row.role).toInt() - 1);
    }
    world.roleCountsKnown = countsKnown;
    if (countsKnown) world.roleCounts = counts;
    world.playerOrder = board.playerOrder;
    world.alivePlayerOrder = board.alivePlayerOrder;
    world.customRoles = board.customRoles;
    if (!viewer || viewer->getRoom() != &m_room) return world;

    // Each invocation has its own exact native Skill-query lifetime. The full
    // public metadata/correction family is proved before the original query;
    // its result is consumed only after this scope has ended.
    const auto queryPublicSkill = [&](const auto &query) -> bool {
        std::unique_ptr<AIRound56WorldSkillQueriesScope> skillSource;
        auto *gameLua=m_room.roomRuntime()->lua().rawState();
        if(!eventOnly&&!inAIRound56ProjectionScope(m_room,gameLua))
            skillSource.reset(new AIRound56WorldSkillQueriesScope(m_room,viewer,gameLua));
        return query();
    };

    const bool hegemony = ServerInfo.EnableHegemony;
    int ownCardBudget = 2048;
    for (const PublicPlayer &row : board.players) {
        ServerPlayer *player = row.player;
        if (!player) continue;
        AIPlayerView playerView = row.view;
        playerView.marksComplete = player == viewer;
        playerView.handPileCount = player->getHandPile().size();
        playerView.playerState = player->getState();
        for (const QString &name : {QStringLiteral("ShenfenCard"), QStringLiteral("LihunCard")})
            playerView.usedHistory.insert(name, player->hasUsed(name));
        if (player == viewer && !eventOnly) {
            std::unique_ptr<AIRound56OwnerCardRulesScope> ownerCardRulesScope;
            auto *gameLua=m_room.roomRuntime()->lua().rawState();
            if(!inAIRound56ProjectionScope(m_room,gameLua))
                ownerCardRulesScope.reset(new AIRound56OwnerCardRulesScope(m_room,player,gameLua));
            const auto addRules = [&](const QString &key, const Card *card) {
                if (!card) return;
                QJsonObject limits, prohibited;
                for (int method = int(Card::MethodNone); method <= int(Card::MethodGet); ++method)
                    limits.insert(QString::number(method), player->isCardLimited(card, Card::HandlingMethod(method)));
                for (ServerPlayer *other : m_room.getAlivePlayers())
                    prohibited.insert(other->objectName(), player->isProhibited(other, card));
                playerView.cardRules.insert(key, QJsonObject{{"limits", limits}, {"prohibited", prohibited}, {"available", card->isAvailable(player)},
                    {"no_response", QJsonArray::fromStringList(card->getTag("NoResponse").toStringList())},
                    {"target_mod_distance", Sanguosha->correctCardTarget(TargetModSkill::DistanceLimit, player, card)},
                    {"target_mod_extra", Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, player, card)},
                    {"target_mod_residue", Sanguosha->correctCardTarget(TargetModSkill::Residue, player, card)}});
                if (card->isKindOf("Analeptic")) {
                    auto rules = playerView.cardRules.value(key).toObject();
                    rules.insert("analeptic_static_available", Analeptic::IsAvailable(player, card));
                    playerView.cardRules.insert(key, rules);
                }
            };
            for (const Card *card : player->getCards("he")) addRules("id:" + QString::number(card->getEffectiveId()), card);
            // Fixed SmartAI gamerule discard passes is_handcard=true. Its
            // native hand-pattern semantics differ from the ordinary limits;
            // attest only this viewer's actual physical hand, never prototypes
            // or a foreign player's private restrictions.
            for (const Card *card : player->getHandcards()) {
                const QString key = "id:" + QString::number(card->getEffectiveId());
                QJsonObject rule = playerView.cardRules.value(key).toObject();
                QJsonObject handLimits = rule.value("limits_hand").toObject();
                handLimits.insert(QString::number(int(Card::MethodDiscard)),
                    player->isCardLimited(card, Card::MethodDiscard, true));
                rule.insert("limits_hand", handLimits);
                playerView.cardRules.insert(key, rule);
            }
            for (const QString &name : {"jink", "slash", "peach", "fire_slash", "thunder_slash"}) {
                Card *probe = Sanguosha->cloneCard(name);
                if (!probe) continue;
                AIRound47XufaProjectionCard xufaProjection(m_room,viewer,probe,name);
                addRules("name:" + name, probe);
                probe->deleteLater();
            }
            for (ServerPlayer *other : m_room.getAllPlayers()) {
                QJsonObject row;
                for (const QString &flags : {"h", "e", "j", "he", "hj", "ej", "hej"})
                    row.insert(flags, player->canDiscard(other, flags));
                for (const Card *card : other->getCards("ej"))
                    row.insert(QString::number(card->getEffectiveId()), player->canDiscard(other, card->getEffectiveId()));
                if (other == viewer) {
                    for (int id : other->handCards()) row.insert(QString::number(id), player->canDiscard(other, id));
                }
                playerView.canDiscard.insert(other->objectName(), row);
            }
        }
        // Query the equipped armor only. A virtual armor skill needs its own
        // visible projection; do not clone hypothetical equipment for the AI.
        playerView.armorEffectKnown = row.armorBasis && (player == viewer || !hegemony);
        if (playerView.armorEffectKnown)
            playerView.activeArmorName = row.activeArmorName;

        const bool seesIdentity = player == viewer || !hegemony
            || row.shownOneGeneral || playerView.dead;
        if (seesIdentity)
            playerView.kingdom = hegemony && player == viewer && !row.shownOneGeneral
                && row.hasActualGeneral1 ? row.hegemonyKingdom : row.kingdom;
        const QString markPrefix = player->objectName() + QString(QChar(0x1f));
        for (auto it = m_markViewers.cbegin(); it != m_markViewers.cend(); ++it) {
            if (!it.key().startsWith(markPrefix)) continue;
            const QString mark = it.key().mid(markPrefix.size());
            if (isMarkVisibleTo(player, mark, viewer)) playerView.publicMarks.insert(mark, player->getMark(mark));
        }
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
                if (player == viewer) {
                    // The shared board may have captured these faces before
                    // their native objects first crossed SWIG. Reproject only
                    // this viewer's still-present pile material, binding the
                    // current face and lifetime token from the same pointer.
                    // Foreign hidden piles never enter this branch.
                    pileView.cards.clear();
                    const QList<int> currentPile = player->getPile(pile.name);
                    for (int id : pileView.cardIds) {
                        if (!currentPile.contains(id)) continue;
                        if (const Card *card = Sanguosha->getCard(id))
                            pileView.cards << makeAICardView(card, true);
                    }
                }
            }
            playerView.piles << pileView;
        }

        for (const PublicSkill &cached : row.skills) {
            if (hegemony && !SkillRuntimeCoordinator::canReceiveSkillInstance(
                    m_room, viewer, player, cached.instance)) {
                playerView.skillsComplete = false;
                continue;
            }
            AISkillView skillView = cached.view;
            skillView.hasPrivateState = player == viewer;
            if (skillView.hasPrivateState) {
                skillView.state = makeAIStateObject(player->getSkillInstanceState(
                    cached.instance.skillName, cached.instance.instanceID));
            }
            playerView.skills << skillView;
        }

        if (player == viewer) {
            // These facts are owner-only, never stored in the shared public cache.
            {
                std::unique_ptr<AIRound56OwnerCardStateScope> ownerStateSource;
                auto *gameLua=m_room.roomRuntime()->lua().rawState();
                if(!eventOnly&&!inAIRound56ProjectionScope(m_room,gameLua))
                    ownerStateSource.reset(new AIRound56OwnerCardStateScope(m_room,viewer,gameLua));
                for (AICardView &card : playerView.equips) projectOwnCardState(card, viewer, ownCardBudget);
                for (AICardPileView &pile : playerView.piles)
                    for (AICardView &card : pile.cards) projectOwnCardState(card, viewer, ownCardBudget);
            }
            playerView.privateFlagsVisible = true;
            playerView.inYinniState = player->inYinniState();
            playerView.aiNoTo = player->property("aiNoTo").toBool();
            playerView.shuangxiongColors = !player->property("shuangxiong_colors_turn").toString().isEmpty();
            playerView.privateFlags = player->getFlagList();
            for (int phase = int(Player::RoundStart); phase <= int(Player::Finish); ++phase)
                playerView.skippedPhases.insert(phase, player->isSkipped(Player::Phase(phase)));
            world.self = playerView;
            {
                std::unique_ptr<AIRound56OwnerCardStateScope> ownerStateSource;
                auto *gameLua=m_room.roomRuntime()->lua().rawState();
                if(!eventOnly&&!inAIRound56ProjectionScope(m_room,gameLua))
                    ownerStateSource.reset(new AIRound56OwnerCardStateScope(m_room,viewer,gameLua));
                foreach (const Card *card, player->getHandcards()) {
                    // The owner sees this exact live hand object even if no prior
                    // gameplay Lua/SWIG callback happened to observe its lifetime.
                    // Issue native identity here, before projecting derived fields;
                    // never synthesize an identity later from a caller-supplied ID.
                    AICardView ownCard = makeAICardView(card, true);
                    projectOwnCardState(ownCard, viewer, ownCardBudget);
                    // Diou reads the exact getHandcards object, before any planning.
                    // Do not project arbitrary tips from disclosed foreign faces.
                    ownCard.knownTips.insert(QStringLiteral("tanban"), card->hasTip(QStringLiteral("tanban")));
                    world.handCards << ownCard;
                }
            }
        } else {
            world.players << playerView;
        }
    }
    {
    std::unique_ptr<AIRound56DistanceScope> distanceScope;
    auto *distanceLua=m_room.roomRuntime()->lua().rawState();
    const bool originalFullProjection=inAIRound56ProjectionScope(m_room,distanceLua);
    if(!eventOnly&&!originalFullProjection)
        distanceScope.reset(new AIRound56DistanceScope(m_room,viewer,distanceLua));
    // The entire public distance callback family is proved before cache choice
    // or any receiver pair. Current cache-key equality never issues source.
    // RoomThread broadcasts every distanceTo_<player> pair to all clients.
    // Preserve that public geometry for strategies such as Quhu, including
    // asymmetric foreign-to-foreign pairs; no provider state is disclosed.
    const auto distancePlayers = m_room.getAlivePlayers();
    const auto distanceSkills = !eventOnly
        ? Sanguosha->getDistanceSkills() : QList<const DistanceSkill *>();
    const quint64 distanceRevision = m_room.roomRuntime()->stateRevision();
    const quint64 distanceWeaponRangeRevision = Weapon::rangeRevision();
    const quint64 skillGeneration = SkillSet::generation();
    if (eventOnly) {
        world.distanceScope = QStringLiteral("none");
    } else if (m_distanceCacheValid && distanceWeaponRangeRevision != 0
        && m_distanceWeaponRangeRevision == distanceWeaponRangeRevision
        && m_distanceRevision == distanceRevision
        && m_distanceSkillGeneration == skillGeneration
        && m_distancePlayers == distancePlayers && m_distanceSkills == distanceSkills) {
        if(distanceScope&&!m_distanceCorrectionSource)
            retireAIRound56Projection(m_room,distanceLua);
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
        // Receipt belongs to this actual producer, including failed attempts.
        m_distanceCorrectionSource=originalFullProjection
            ||(distanceScope&&distanceScope->accepted());
        // A callback that mutates gameplay must never publish a reusable stale table.
        m_distanceCacheValid = Weapon::rangeRevisionCurrent(distanceWeaponRangeRevision)
            && distanceRevision == m_room.roomRuntime()->stateRevision()
            && skillGeneration == SkillSet::generation();
        if (m_distanceCacheValid) {
            m_distanceRevision = distanceRevision;
            m_distanceSkillGeneration = skillGeneration;
            m_distanceWeaponRangeRevision = distanceWeaponRangeRevision;
            m_distancePlayers = distancePlayers;
            m_distanceSkills = distanceSkills;
            m_worldDistances = world.distances;
        }
    }
    } // exact distance lifetime ends before discard/event/skill projection
    if (!eventOnly) foreach (const int cardId, m_room.getDiscardPile()) {
        if (const Card *card = Sanguosha->getCard(cardId))
            world.discardPile << makeAICardView(card, true);
    }
    if (!eventOnly) foreach (const AIEventView &event, m_events) {
        if (event.privateEvent && event.privateViewer != viewer->objectName()) continue;
        AIEventView visible = event;
        // Resolve attribution only through skills the observer is entitled to know.
        const QString ownerSkill = !event.reason.isEmpty() ? event.reason : event.cardSkill;
        if (event.kind == QStringLiteral("damage") || event.kind == QStringLiteral("judge")) {
            bool complete = true;
            QString owner;
            for (ServerPlayer *candidate : m_room.getAllPlayers(true)) {
                bool skillsComplete = candidate == viewer || !ServerInfo.EnableHegemony
                    || candidate->hasShownAllGenerals() || candidate->isDead();
                if (!skillsComplete) { complete = false; break; }
                if (!ownerSkill.isEmpty() && queryPublicSkill([&] { return candidate->hasSkill(ownerSkill); })) {
                    owner = candidate->objectName(); break;
                }
            }
            visible.details.insert("intention_from_complete", complete);
            if (complete) visible.details.insert("intention_from", owner);
        }
        if (visible.privateViewer != viewer->objectName())
            visible.privateCardIds.clear();
        visible.privateViewer.clear();
        world.events << visible;
    }
    if (!eventOnly) {
        QJsonObject fillContexts;
        QStringList names;
        {
            std::unique_ptr<AIRound56WorldSkillQueriesScope> skillSource;
            auto *gameLua=m_room.roomRuntime()->lua().rawState();
            if(!inAIRound56ProjectionScope(m_room,gameLua))
                skillSource.reset(new AIRound56WorldSkillQueriesScope(m_room,viewer,gameLua));
            for (const Skill *skill : viewer->getSkillList(true))
                if (skill && (skill->isLordSkill() ? viewer->hasLordSkill(skill->objectName()) : viewer->hasSkill(skill)))
                    names << skill->objectName();
        }
        for (const QString &mark : viewer->getMarkNames()) {
            if (viewer->getMark(mark) > 0 && mark.startsWith("ViewAsSkill_")) {
                QString name = mark;
                name.replace("ViewAsSkill_", ""); name.replace("Effect", "");
                if (!names.contains(name)) names << name;
            }
        }
        for (const QString &name : names) {
            const ViewAsSkill *vs = Sanguosha->getViewAsSkill(name);
            const int instanceId = skillActionInstanceId(viewer, name);
            const AiLegacyRequestView context = skillActionContext(viewer, name,
                CardUseStruct::CARD_USE_REASON_PLAY, QString(), QString(), Card::MethodUse);
            QJsonObject value{{"known", true}, {"instance_id", instanceId},
                {"has_view_as", vs != nullptr}, {"enabled_at_play", !vs || vs->isEnabledAtPlay(viewer)},
                {"request_valid", context.isValid()}};
            if (context.isValid()) value.insert("request", QJsonObject{{"viewer", viewer->objectName()}, {"reason", int(context.getReason())},
                {"pattern", context.getPattern()}, {"prompt", context.getPrompt()},
                {"handling_method", int(context.getHandlingMethod())},
                {"skill_action", responseIdentity(context.request.skillActionContext)}});
            fillContexts.insert(name, value);
        }
        world.strategyFacts.insert("fill_skill_contexts", fillContexts);
        QJsonObject notifiedPiles;
        for (const QString &name : names) {
            const ViewAsSkill *vs = Sanguosha->getViewAsSkill(name);
            if (!vs) continue;
            for (const QString &pileName : vs->getExpandPile().split(',')) {
                if (!pileName.startsWith('#') || notifiedPiles.contains(pileName)) continue;
                const QString tagKey = pileName.mid(1) + "ForAI";
                const QVariant notified = viewer->getTag(tagKey);
                const QJsonObject disclosed = readAIStrategyNotifiedPile(m_room, viewer, pileName);
                const QJsonObject disclosedCards = disclosed.value("cards").toObject();
                const QJsonObject disclosedLocations = disclosed.value("locations").toObject();
                QJsonArray ids, cards, currentIds;
                QJsonObject locations;
                bool complete = true;
                for (const QVariant &rawId : notified.toList()) {
                    bool valid = false; const int id = rawId.toInt(&valid);
                    if (!valid || id < 0) { complete = false; continue; }
                    const QString key = QString::number(id);
                    QJsonObject visibleLocation;
                    const auto locate = [&](const QList<AICardView> &zone, const QString &owner, Player::Place place) {
                        for (const AICardView &known : zone) if (known.effectiveId == id) {
                            visibleLocation = QJsonObject{{"owner", owner}, {"place", int(place)}}; break;
                        }
                    };
                    locate(world.handCards, viewer->objectName(), Player::PlaceHand);
                    QList<AIPlayerView> visiblePlayers = world.players; visiblePlayers.prepend(world.self);
                    for (const AIPlayerView &known : visiblePlayers) {
                        locate(known.equips, known.objectName, Player::PlaceEquip);
                        locate(known.judgingArea, known.objectName, Player::PlaceDelayedTrick);
                        locate(known.knownCards, known.objectName, Player::PlaceHand);
                        for (const AICardPileView &pile : known.piles)
                            if (pile.open) locate(pile.cards, known.objectName, Player::PlaceSpecial);
                    }
                    locate(world.discardPile, QString(), Player::DiscardPile);
                    QJsonObject printed = disclosedCards.value(key).toObject();
                    if (!visibleLocation.isEmpty()) {
                        // Only independently visible cards may refresh a remembered
                        // notification from the current authoritative wrapped card.
                        const Card *card = Sanguosha->getCard(id);
                        if (card) {
                            printed = publicDecisionCard(card);
                            printed.remove("flags"); printed.remove("source_skill");
                            printed.insert("id", id); printed.insert("effective_id", id);
                            printed.insert("virtual_card", false); printed.insert("subcards", QJsonArray());
                            printed.insert("can_recast", card->canRecast());
                            printed.insert("target_fixed", card->targetFixed());
                            printed.insert("handling_method", int(card->getHandlingMethod()));
                        }
                    }
                    if (visibleLocation.isEmpty()) {
                        const auto publicFace = readAIStrategyPublicDisclosure(m_room, viewer, id);
                        if (publicFace.value("known").toBool()) {
                            printed = publicFace.value("card").toObject();
                            visibleLocation = QJsonObject{{"owner", publicFace.value("owner")},
                                {"place", publicFace.value("place")}};
                        }
                    }
                    if (printed.isEmpty()) { complete = false; continue; }
                    ids.append(id); cards.append(printed);
                    if (!visibleLocation.isEmpty() || (disclosed.value("revision").toString() == QString::number(world.revision)
                        && disclosed.value("current_card_ids").toArray().contains(id)))
                        currentIds.append(id);
                    locations.insert(key, !visibleLocation.isEmpty() ? visibleLocation
                        : disclosedLocations.value(key).toObject());
                }
                notifiedPiles.insert(pileName, QJsonObject{{"tag_key", tagKey}, {"complete", complete},
                    {"card_ids", ids}, {"current_card_ids", currentIds}, {"cards", cards}, {"locations", locations}});
            }
        }
        world.strategyFacts.insert("notified_piles", notifiedPiles);
    }
    QJsonObject labels, distanceFixOne;
    const auto addLabels = [&](const AIPlayerView &value) {
        const ServerPlayer *player = m_room.findPlayerByObjectName(value.objectName, true);
        if (!player) return;
        QString name = value.generalName;
        if (name.isEmpty()) name = "anjiang";
        QString log = Sanguosha->translate(name);
        if (!value.general2Name.isEmpty()) log += "/" + Sanguosha->translate(value.general2Name);
        if (name == "anjiang") log += QString("[%1]").arg(value.seat);
        labels.insert(value.objectName, QJsonObject{{"screen_name", player->screenName()}, {"log_name", log}});
    };
    addLabels(world.self); for (const auto &value : world.players) addLabels(value);
    world.strategyFacts.insert("player_labels", labels);
    if (eventOnly) {
        for (const ServerPlayer *from : m_room.getAlivePlayers()) {
            QJsonObject row;
            for (const ServerPlayer *to : m_room.getAlivePlayers()) row.insert(to->objectName(), from->distanceTo(to, 1));
            distanceFixOne.insert(from->objectName(), row);
        }
        world.strategyFacts.insert("distance_fix_one", distanceFixOne);
    }
    projectAIStrategyWorld(world, m_room, viewer, eventOnly);
    projectAIRound34PlanningCardCounts(world, m_room, viewer);
    if (viewer) {
        QJsonObject prototypes;
        for (const QString &name : {"#dl_chaju", "#dl_pindi", "#dl_yanxiao", "#dl_liuli"}) {
            std::unique_ptr<SkillCard> prototype(LuaSkillCard::Parse(name + ":.:"));
            prototypes.insert(name, bool(prototype));
        }
        world.strategyFacts.insert("dragonloke", QJsonObject{{"legacy_prototypes", prototypes}});
    }

    // Round17 observations are fixed owner/public vocabulary, never arbitrary
    // tag lookups. A missing donor tag retains QVariant's source default.
    if (viewer) {
        // These properties and the pile length are owner/public observations.
        // No draw-pile identity is implied by a known length.
        world.strategyFacts.insert("genius", QJsonObject{
            {"yishi_skills", viewer->getTag("f_yishi").toString()},
            {"jianyuan_record", viewer->property("SkillDescriptionRecord_f_jianyuan").toString()}});
        world.strategyFacts.insert("nybeauty", QJsonObject{
            {"draw_count", m_room.getDrawPile().size()},
            {"properties", QJsonObject{{"nytonglisuit", viewer->property("nytonglisuit").toString()},
                {"nytonglipattern", viewer->property("nytonglipattern").toString()}}}});
        world.strategyFacts.insert("challengedeveloper", QJsonObject{
            {"pofeng_num", viewer->getTag("dev_pofeng_num").toInt()},
            {"juanlao_card", viewer->property("dev_juanlao_card").toString()},
            {"play_clear_epoch", world.strategyFacts.value("strategy_play_clear_epochs").toObject().value(viewer->objectName())}});
        world.strategyFacts.insert("source_enable_hegemony", Config.value("EnableHegemony", false).toBool());
        if (!Config.value("EnableHegemony", false).toBool()
            && viewer->getTag("roles").toString().isEmpty()
            && !viewer->getTag("triggerEvent").isValid()
            && !viewer->getTag("triggerEventData").isValid())
            world.strategyFacts.insert("basara", QJsonObject{{"hegemony_variant", false},
                {"trigger_event", "other"}, {"generals", QJsonArray{}}});
        { // Owner card-need observations may be used for another player's skill.
            QJsonObject facts;
            for (const QString &key : {"tiangong_targets_AI", "tiangong_schemesetup_AI", "linglong_loseskillData"})
                facts.insert(key, viewer->getTag(key).toString());
            // toMaxHp/toHpLost/toDamage return default structs on wrong type.
            facts.insert("linglong_maxhpData", QJsonObject{{"change", viewer->getTag("linglong_maxhpData").value<MaxHpStruct>().change}});
            facts.insert("linglong_losehpData", QJsonObject{{"lose", viewer->getTag("linglong_losehpData").value<HpLostStruct>().lose}});
            facts.insert("linglong_damageData", QJsonObject{{"damage", viewer->getTag("linglong_damageData").value<DamageStruct>().damage}});
            QJsonObject eagle;
            for (const Card *card : viewer->getCards("he"))
                eagle.insert(QString::number(card->getEffectiveId()), card->getTag("jlyingshi_eagle").toBool());
            facts.insert("jlyingshi_eagle", eagle);
            world.strategyFacts.insert("hunliesp", facts);
        }
        if (queryPublicSkill([&] { return viewer->hasSkills("jinfan|jinfanTake|duYinling|du_zhouxuan|jiuwei|jieyou"); })) {
            QJsonObject prototypes, flags;
            for (const QString &name : {"#jfTakeCard", "#duYinlingCard", "#du_zhouxuanCard"}) {
                std::unique_ptr<SkillCard> card(LuaSkillCard::Parse(name + ":.:"));
                prototypes.insert(name, bool(card));
            }
            for (const auto *player : m_room.getAllPlayers(true))
                flags.insert(player->objectName(), player->hasFlag("jiuwei_target"));
            world.strategyFacts.insert("du", QJsonObject{{"legacy_prototypes", prototypes}, {"jiuwei_target", flags}});
        }
    }

    // Fixed Round16 source vocabulary; no arbitrary tag/flag query bridge.
    if (viewer) {
        if (queryPublicSkill([&] { return viewer->hasSkills("se_mopao|se_kuixin|se_huixiang"); })) {
            QJsonObject prototypes, flags;
            for (const QString &name : {"#se_mopaocard", "#se_kuixincard", "#se_huixiang"}) {
                std::unique_ptr<SkillCard> prototype(LuaSkillCard::Parse(name + ":.:"));
                prototypes.insert(name, bool(prototype));
            }
            if (queryPublicSkill([&] { return viewer->hasSkill("se_kuixin"); }))
                for (const ServerPlayer *target : m_room.getAlivePlayers())
                    flags.insert(target->objectName(), target->hasFlag("se_kuixin_used"));
            world.strategyFacts.insert("dmptouhou", QJsonObject{{"legacy_prototypes", prototypes}, {"kuixin_used", flags}});
        }
        if (queryPublicSkill([&] { return viewer->hasSkills("shenjiZHCMT|zhuiluZHCMT|XiansiZHCMT|XiansiZHCMTSlash|zhengsiZHCMT"); }))
            world.strategyFacts.insert("zhenghuo_cmt", QJsonObject{{"slash_available", Slash::IsAvailable(viewer)}});
        // etushou2 is explicitly broadcast by Room::setPlayerFlag; this does
        // not disclose whether an otherwise hidden skill is possessed.
        {
            std::unique_ptr<AIRound56OwnerSlashScope> ownerSlashScope;
            auto *gameLua=m_room.roomRuntime()->lua().rawState();
            if(!inAIRound56ProjectionScope(m_room,gameLua))
                ownerSlashScope.reset(new AIRound56OwnerSlashScope(m_room,viewer,gameLua));
            world.strategyFacts.insert("owner_can_slash_without_crossbow", viewer->canSlashWithoutCrossbow());
        }
        const auto ownTagPlayer = [&](const char *key) -> QJsonValue {
            const auto value = viewer->getTag(key);
            const auto *target = value.canConvert<ServerPlayer *>() ? value.value<ServerPlayer *>() : nullptr;
            return target && target->getRoom() == &m_room ? QJsonValue(target->objectName()) : QJsonValue(false);
        };
        world.strategyFacts.insert("guo", QJsonObject{{"jintuishi_from", ownTagPlayer("jintuishi_from")}});
        QJsonArray caozhaoNames;
        for (const auto &name : viewer->property("CaozhaoNames").toStringList()) caozhaoNames.append(name);
        world.strategyFacts.insert("li", QJsonObject{{"jinqingleng_now_target", viewer->property("jinqingleng_now_target").toString()},
            {"CaozhaoNames", caozhaoNames}, {"xianzhu_slash_from", ownTagPlayer("xianzhu_slash_from")}});
        QJsonObject ownerTags;
        for (const QString &key : {"mcc_phasedraw_count", "mcc_phasedraw_num", "mcc_defaultslash_num"})
            ownerTags.insert(key, viewer->getTag(key).toInt());
        world.strategyFacts.insert("mojiang", QJsonObject{{"owner_tags", ownerTags}});
        QJsonObject sourceVisibleHands;
        for (const auto *target : m_room.getAllPlayers(true)) {
            QJsonArray ids;
            const QString flag = QString("visible_%1_%2").arg(viewer->objectName(), target->objectName());
            for (const auto *card : target->getHandcards())
                if (card && (card->hasFlag("visible") || card->hasFlag(flag))) ids.append(card->getEffectiveId());
            sourceVisibleHands.insert(target->objectName(), ids);
        }
        world.strategyFacts.insert("source_visible_hand_ids", sourceVisibleHands);
        QJsonObject generalSkills;
        const auto skillsOf = [&](const QString &name) -> QJsonValue {
            if (name.isEmpty()) return QJsonValue(); // concealed is unknown, not absent
            const auto *general = Sanguosha->getGeneral(name);
            if (!general) return false;
            QJsonArray rows;
            for (const auto *skill : general->getSkillList())
                if (skill) rows.append(QJsonObject{{"name", skill->objectName()}, {"visible", skill->isVisible()},
                    {"lord", skill->isLordSkill()}, {"frequency", int(skill->getFrequency())}, {"limited", skill->isLimitedSkill()}});
            return rows;
        };
        const auto addGeneralSkills = [&](const AIPlayerView &view) {
            QJsonObject row;
            const auto primary = skillsOf(view.generalName);
            if (!primary.isNull() && !primary.isUndefined()) row.insert("primary", primary);
            // Empty second general is absent only outside concealed general mode.
            const auto secondary = skillsOf(view.general2Name);
            if (!secondary.isNull() && !secondary.isUndefined()) row.insert("secondary", secondary);
            else if (!world.enableHegemony) row.insert("secondary", false);
            if (view.skillsComplete) {
                const auto *player = m_room.findPlayerByObjectName(view.objectName, true);
                QJsonObject effective;
                if (player) for (const QString &part : {QStringLiteral("primary"), QStringLiteral("secondary")})
                    for (const auto &value : row.value(part).toArray()) {
                        const auto skill = value.toObject();
                        if (skill.value("visible").toBool() && skill.value("lord").toBool())
                            effective.insert(skill.value("name").toString(), queryPublicSkill([&] { return player->hasLordSkill(skill.value("name").toString(), true); }));
                    }
                row.insert("lord_effective_including_lose", effective);
            }
            generalSkills.insert(view.objectName, row);
        };
        addGeneralSkills(world.self); for (const auto &view : world.players) addGeneralSkills(view);
        world.strategyFacts.insert("lei", QJsonObject{{"general_skills", generalSkills}});

        QJsonObject presentCards, fuleiCounts;
        const auto publicCardFact = [&](const AICardView &view) {
            const auto *card = Sanguosha->getCard(view.effectiveId);
            if (card && card->isKindOf("Fulei")) fuleiCounts.insert(card->toString(), m_room.getTag("Fulei" + card->toString()).toInt());
        };
        for (const auto &view : world.handCards) {
            publicCardFact(view);
            const auto *card = Sanguosha->getCard(view.effectiveId);
            const auto *engine = Sanguosha->getEngineCard(view.effectiveId);
            if (card && engine) presentCards.insert(QString::number(view.effectiveId), engine->objectName() == card->objectName() && engine->isGift());
        }
        for (const auto &view : world.self.judgingArea) publicCardFact(view);
        for (const auto &player : world.players) for (const auto &view : player.judgingArea) publicCardFact(view);
        world.strategyFacts.insert("yongjian", QJsonObject{{"card_is_present", presentCards}, {"fulei_counts", fuleiCounts},
            {"defensive_horse_zhanxiang", QJsonObject{{viewer->objectName(), [&] {
                std::unique_ptr<AIRound56OwnerEquipmentQueriesScope> equipmentSource;
                auto *gameLua=m_room.roomRuntime()->lua().rawState();
                if(!inAIRound56ProjectionScope(m_room,gameLua))
                    equipmentSource.reset(new AIRound56OwnerEquipmentQueriesScope(m_room,viewer,gameLua));
                return viewer->hasDefensiveHorse("yj_zhanxiang");
            }()}}}});
        QJsonObject hayateFlags;
        for (const QString &name : {"haremu_target", "youshui_target"}) {
            QJsonObject values;
            for (const auto *target : m_room.getAlivePlayers()) values.insert(target->objectName(), target->hasFlag(name));
            hayateFlags.insert(name, values);
        }
        world.strategyFacts.insert("hayate", QJsonObject{{"owner_tags", QJsonObject{{"QiubangUsed", viewer->getTag("QiubangUsed").toInt()}}},
            {"target_flags", hayateFlags}});
        world.strategyFacts.insert("doudizhu", QJsonObject{{"owner_tags", QJsonObject{
            {"qixinCaojieHp", viewer->getTag("qixinCaojieHp").toInt()}, {"qixinLiuxieHp", viewer->getTag("qixinLiuxieHp").toInt()}}}});
        QJsonObject etushou;
        for (const ServerPlayer *target : m_room.getAlivePlayers())
            etushou.insert(target->objectName(), target->hasFlag("etushou2"));
        world.strategyFacts.insert("emjr", QJsonObject{{"etushou2", etushou}});
        if (queryPublicSkill([&] { return viewer->hasSkills("jinyishi|jinciwei"); })) {
            const auto playerName = [&](const char *key) -> QJsonValue {
                const QVariant value = viewer->getTag(key);
                const ServerPlayer *target = value.canConvert<ServerPlayer *>() ? value.value<ServerPlayer *>() : nullptr;
                return target && target->getRoom() == &m_room ? QJsonValue(target->objectName()) : QJsonValue(false);
            };
            QJsonArray ids;
            for (const auto &value : viewer->getTag("jinyishiForAI").toList()) ids.append(value.toInt());
            world.strategyFacts.insert("bei", QJsonObject{{"jinyishi_card_ids", ids},
                {"jinyishi_from", playerName("jinyishi_from")}, {"jinciwei_player", playerName("jinciwei-player")}});
        }
    }

    // A fixed owner-scoped vocabulary for standard's prompt context. No tag
    // names supplied by policy and no foreign private container are queried.
    if (viewer) {
        QJsonObject standard = world.strategyFacts.value("standard").toObject();
        for (const QString &key : {"current_damage", "fenwei_use", "qianxun_effect", "liuli_card", "lianying_move_reason"})
            standard.insert(key, false);
        const QVariant damageValue = m_room.getTag("CurrentDamageStruct");
        if (damageValue.canConvert<DamageStruct>()) {
            const DamageStruct damage = damageValue.value<DamageStruct>();
            standard.insert("current_damage", QJsonObject{{"from", damage.from ? damage.from->objectName() : QString()},
                {"to", damage.to ? damage.to->objectName() : QString()}, {"amount", damage.damage},
                {"nature", int(damage.nature)}, {"reason", damage.reason}, {"chain", damage.chain},
                {"transfer", damage.transfer}, {"by_user", damage.by_user}, {"card", publicDecisionCard(damage.card)}});
        }
        const QVariant fenweiValue = viewer->getTag("fenwei");
        if (fenweiValue.canConvert<CardUseStruct>()) {
            const CardUseStruct use = fenweiValue.value<CardUseStruct>();
            QJsonArray targets; for (const ServerPlayer *target : use.to) if (target) targets.append(target->objectName());
            standard.insert("fenwei_use", QJsonObject{{"from", use.from ? use.from->objectName() : QString()},
                {"to", targets}, {"card", publicDecisionCard(use.card)}});
        }
        const QVariant moveValue = viewer->getTag("LianyingMoveData");
        if (moveValue.canConvert<CardsMoveOneTimeStruct>())
            standard.insert("lianying_move_reason", moveValue.value<CardsMoveOneTimeStruct>().reason.m_skillName);
        const QVariant effectValue = viewer->getTag("QianxunEffectData");
        if (effectValue.canConvert<CardEffectStruct>()) {
            const CardEffectStruct effect = effectValue.value<CardEffectStruct>();
            standard.insert("qianxun_effect", QJsonObject{{"from", effect.from ? effect.from->objectName() : QString()},
                {"to", effect.to ? effect.to->objectName() : QString()}, {"card", publicDecisionCard(effect.card)},
                {"offset_num", effect.offset_num}});
        }
        const Card *liuli = viewer->getTag("liuli-card").value<const Card *>();
        if (liuli) standard.insert("liuli_card", publicDecisionCard(liuli));
        world.strategyFacts.insert("standard", standard);
    }

    if (eventOnly) world.strategyFacts.insert("event_conversion_policy", eventConversionPolicy(m_room, viewer));
    projectAIStrategyYjcm2012(world, m_room, viewer);
    projectAIYjcm2014(world, m_room, viewer, [](const Card *card) { return publicDecisionCard(card, true); });
    projectAIYjcm2015(world, m_room, viewer, [](const Card *card) { return publicDecisionCard(card, false); });
    projectAIStrategyHistory(world, m_room, viewer);
    projectAIConstructionFacts(world);
    projectAIResponseObservations(world, m_room, viewer);
    projectAIStrategyRound21OriginalWorld(world, m_room, viewer);
    projectAIRound25ImagineWorld(world, m_room, viewer);
    projectAIRound25PlanningMetadata(world, m_room);
    projectAIRound31PlanningMetadata(world, m_room);
    projectAIRound25OriginalWorld(world, m_room, viewer);
    projectAIRound26OriginalWorld(world, m_room, viewer);
    projectAIRound27OriginalWorld(world, m_room, viewer);
    projectAIRound28ExtraWorld(world, m_room, viewer);
    projectAIRound45InovationWorld(world, m_room, viewer);
    projectAIRound45InovationProviders(world,m_room,viewer);
    projectAIRound29ExtraWorld(world, m_room, viewer);
    projectAIRound30TargetRecommend(world, m_room, viewer);
    projectAIRound31Choice(world, m_room, viewer);
    projectAIRound34Choice(world, m_room, viewer);
    projectAIRound45PlanningFacts(world,m_room,viewer);
    projectAIRound36Formation(world, m_room, viewer);
    projectAIRound31IntentionWorld(world, m_room, const_cast<ServerPlayer *>(viewer));
    if (queryPublicSkill([&] { return viewer->hasSkill("ollongdan"); }))
        world.strategyFacts.insert("ol_st_standard", QJsonObject{{"analeptic_available", Analeptic::IsAvailable(viewer)}});
    // Original phase/removal and keep-value policies ask isFriend(a, b).
    // Build every relation row from THIS observer's already filtered world and
    // policy mind; never transpose a viewer row or read another actor's mind.
    // Compact non-hegemony requests retain their existing projection contract.
    m_room.roomRuntime()->ai().projectDefenseHistory(world);
    AiLuaRuntime::evaluateModePolicy(m_room.roomRuntime()->lua(), world,
        compactPolicy && !world.enableHegemony);
    return world;
}

AIWorldView AiDecisionCoordinator::buildWorldView(ServerPlayer *viewer, bool compactPolicy,
                                                 bool eventOnly) const
{
    AIWorldView world;
    world.modeId = m_room.getMode();
    world.revision = m_room.roomRuntime()->stateRevision();
    if (!viewer || viewer->getRoom() != &m_room) return world;
    AIIdentityProjectionScope identityScope(m_room, viewer);
    const quint64 rangeRevision = Weapon::rangeRevision();
    world = projectWorldView(publicBoard(), viewer, compactPolicy, eventOnly);
    world.weaponRangeTracked = true;
    world.weaponRangeRevision = Weapon::rangeRevisionCurrent(rangeRevision) ? rangeRevision : 0;
    return world;
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
    // This request names the response's intended viewer. Only attest the two
    // public literal comparisons when the live native context agrees; an event,
    // a prospective request, or another actor's ambient response is not evidence.
    const auto *state = m_room.roomState();
    if (player && player->getRoom() == &m_room
        && (kind == AIRequest::RespondCard || kind == AIRequest::UseCard)
        && (reason == CardUseStruct::CARD_USE_REASON_RESPONSE
            || reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
        && state->getCurrentCardUseReason() == reason
        && state->getCurrentCardUsePattern() == pattern) {
        const QString nativePattern = state->getCurrentCardUsePattern();
        request.originalAocaiPattern = {{"viewer", request.viewerObjectName},
            {"decision", request.getDecisionId()}, {"revision", request.getStateRevision()},
            {"slash", nativePattern == "slash"}, {"jink", nativePattern == "jink"}};
    }
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
    // --ai off selects the native fallback. It must not build the quadratic
    // isolated snapshot or allow an isolated Lua handler to override that AI.
    if (!Config.EnableAI && (!player || !m_room.externalAgent(player->objectName())))
        return request;
    // Build the request from a read-only board snapshot; within one revision, evaluate matching distance, attack-range, and target-modifier queries once.
    TargetModMemoScope targetModMemo([this]() { return m_room.roomRuntime()->stateRevision(); });
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
        ResponseNativeScope requestScope(m_room, player, reason, pattern);
        foreach (const SkillInstance &instance, player->getSkillInstances()) {
            AiSkillActionContext actionContext;
            if (buildSkillActionContext(player, instance, reason, pattern, actionContext))
                request.skillActions << actionContext;
        }
        int projectionBudget = 16384;
        {
            std::unique_ptr<AIRound56OwnerCandidatesScope> ownerCandidatesScope;
            auto *gameLua=m_room.roomRuntime()->lua().rawState();
            if(kind==AIRequest::Activate&&reason==CardUseStruct::CARD_USE_REASON_PLAY
                &&!inAIRound56ProjectionScope(m_room,gameLua))
                ownerCandidatesScope.reset(new AIRound56OwnerCandidatesScope(m_room,player,gameLua));
            foreach (const Card *card, player->getHandcards())
                request.cardCandidates << makeAICardCandidate(m_room, player, card, method,
                                                              kind, pattern, projectionBudget);
            foreach (const Card *card, player->getEquips())
                request.cardCandidates << makeAICardCandidate(m_room, player, card, method,
                                                              kind, pattern, projectionBudget);
        }
        const int borrowedId = request.worldView.strategyFacts.value("yjcm2015").toObject()
            .value("taoxi_borrowed").toObject().value("card_id").toInt(-1);
        if (liveTaoxiBorrow(m_room, player, request.worldView, borrowedId))
            request.cardCandidates << makeAICardCandidate(m_room, player, Sanguosha->getCard(borrowedId),
                method, kind, pattern, projectionBudget);
        if (kind == AIRequest::UseCard || kind == AIRequest::RespondCard) {
            QSet<int> projected;
            for (const AICardCandidateView &candidate : request.cardCandidates) projected.insert(candidate.cardId);
            for (int id : player->getHandPile()) {
                if (projected.contains(id)) continue;
                if (const Card *card = Sanguosha->getCard(id))
                    request.cardCandidates << makeAICardCandidate(m_room, player, card, method,
                        kind, pattern, projectionBudget);
            }
        }
        // The authorization tickets are handed out last, so every candidate this
        // request offered carries one and nothing outside the list can claim one.
        for (int index = 0; index < request.cardCandidates.size(); ++index)
            request.cardCandidates[index].candidateId = index + 1;
        buildCardConversions(player, request, projectionBudget);
    } else if (player && kind != AIRequest::TriggerOrder
        && request.worldView.modePolicy.value("projection_scope").toString() != "local_decision") {
        // Non-card policy still calls getCards/getCardsNum. Its response-view
        // metadata and lazy producers must be available for valuation too;
        // the decision-kind dispatcher never admits those receipts as an action.
        int projectionBudget = 4096;
        buildCardConversions(player, request, projectionBudget);
    }
    if (player && kind == AIRequest::UseCard
        && (pattern == "@@dl_liuli" || pattern == "@@LuaHuowei")) {
        projectDecisionContext(player, QVariant(), request);
        // UseCard has no generic options payload. Only these audited public /
        // own-question DTOs enter its viewer-scoped serialized world context.
        const QString key = pattern == "@@dl_liuli" ? "dragonloke_liuli" : "lua_old_enemy_girls";
        if (request.choiceOptions.context.contains(key))
            request.worldView.decisionContext.insert(key, request.choiceOptions.context.value(key));
    }
    if (player && kind == AIRequest::UseCard && pattern == "@@tianxiang") {
        const QVariant damage = player->getTag("TianxiangDamage");
        if (damage.canConvert<DamageStruct>()) projectDecisionContext(player, damage, request);
    }
    projectAIStrategyRequest(request, m_room, player);
    projectAIStrategyResponseContext(request, m_room, player, QVariant(), request.choiceOptions.context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIStrategyRound24Response(request, m_room, player, QVariant(), request.choiceOptions.context);
    projectAIStrategyRetrialContext(request, m_room, player, QVariant(), request.choiceOptions.context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIStrategyChoiceContext(request, m_room, player, QVariant(), request.choiceOptions.context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIConstructionContext(request, m_room, player,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound21PlanningContext(request, m_room, player,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound24FeiDIYContext(request, m_room, player,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound24ChoiceContext(request, m_room, player);
    projectAIRound25ChoiceContext(request, m_room, player, QVariant(), request.choiceOptions.context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound26ChoiceContext(request, m_room, player, QVariant(), request.choiceOptions.context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound27ChoiceContext(request, m_room, player, QVariant(), request.choiceOptions.context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIStrategyRound25Response(request, m_room, player, request.choiceOptions.context);
    projectAIRound25OriginalContext(request, m_room, player, QVariant(),
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound26OriginalContext(request, m_room, player, QVariant(),
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound26PlanningContext(request, m_room, player, QVariant(), request.choiceOptions.context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound28PlanningContext(request, m_room, player, QVariant(), request.choiceOptions.context,
        [](const Card *card) { return publicDecisionCard(card); },
        [](const Card *card, int, const QString &) { return publicDecisionCard(card); });
    projectAIRound27PlanningContext(request, m_room, player, QVariant(), request.choiceOptions.context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound21ChoiceContext(request, m_room, player, request.choiceOptions.context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound22OlClanPlanContext(request, m_room, player);
    projectAIRound22ChoiceContext(request, m_room, player, request.choiceOptions.context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIStrategyRound22OlClanObservations(request, m_room, player, QVariant(), request.choiceOptions.context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIStrategyYCZH2017(request, m_room, player, [&](const Card *card) {
        return publicDecisionCard(card, request.choiceOptions.reason != "funan");
    });
    projectAIRound24OriginalContext(request, m_room, player, QVariant(),
        [](const Card *card) { return publicDecisionCard(card); });
    if (player && kind == AIRequest::UseCard && pattern == "@@guangyu")
        projectAIRound45InovationContext(request,m_room,player,QVariant());
    // UseCard has no generic serialized options table. Its context is built
    // above only by the audited viewer-scoped question projectors; expose those
    // same pure values through world context, matching getDecisionContext's
    // options-over-world merge for non-UseCard requests. This does not project
    // arbitrary Room/Player tags or grant conversion/payment authority.
    if (kind == AIRequest::Activate && request.choiceOptions.context.contains("round22_choice"))
        request.worldView.decisionContext.insert("round22_choice", request.choiceOptions.context.value("round22_choice"));
    if (kind == AIRequest::Activate)
        for (const QString &key : {QStringLiteral("round26_choice"), QStringLiteral("round27_choice"), QStringLiteral("original26")})
            if (request.choiceOptions.context.contains(key)) request.worldView.decisionContext.insert(key, request.choiceOptions.context.value(key));
    if (kind == AIRequest::UseCard) {
        for (auto it = request.choiceOptions.context.constBegin();
             it != request.choiceOptions.context.constEnd(); ++it)
            request.worldView.decisionContext.insert(it.key(), it.value());
    }
    if (kind == AIRequest::UseCard && pattern == "@@jieyingh") {
        projectDecisionContext(player, QVariant(), request);
        for (const QString &key : {QStringLiteral("nested_delegate"),QStringLiteral("yingbian")})
            if (request.choiceOptions.context.contains(key)) request.worldView.decisionContext.insert(key, request.choiceOptions.context.value(key));
    }
    projectAIRound27JieyinghQuestion(request, m_room, player);
    projectAIRound49Guhuo(request, m_room, player);
    projectAIRound49CurrentFields(request,m_room,player);
    projectAIRound49Gongjian(request,m_room,player);
    projectAIRound49OwnerNames(request,m_room,player);
    projectAIRound49OwnerBools(request,m_room,player);
    projectAIRound50Planning(request,m_room,player);
    projectAIRound51Collateral(request,m_room,player);
    projectAIRound53Collateral(request,m_room,player);
    projectAIRound52Rangyi(request,m_room,player);
    projectAIRound49Notified(request,m_room,player);
    projectAIRound49NotifiedContexts(request,m_room,player);
    projectAIRound49Bozhi(request,m_room,player);
    projectAIRound49MaotuYinhu(request,m_room,player);
    projectAIRound29ZhifengQuestion(request, m_room, player);
    projectAIStrategyRound26Response(request, m_room, player);
    if (aiRound28NiwoResponseQuestion(request,m_room,player)) projectReadonlyOlClanPlanning(player,request);
    // Finite owner questions also include UseCard. Publish only after the
    // completed world/options handoff; the producer synchronizes both stores.
    projectAIRound45Newgenerals(request,m_room,player,[](const Card *card){return publicDecisionCard(card);});
    projectAIRound50YuriQuestion(request,m_room,player,[](const Card*card){return publicDecisionCard(card);});
    projectAIRound51Jijiao(request,m_room,player);
    bindAIRound53XufaQuestion(request,player);
    bindAIRound53XuanzhuQuestion(request,player);
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
                                                 AIRequest &request, int &projectionBudget,
                                                 bool scopesOnly) const
{
    request.cardConversions.clear();
    request.conversionsEnumerated = false;
    request.stagedActionsComplete = false;
    if (!player) return false;
    EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
    LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
    ResponseNativeScope requestScope(m_room, player, request.reason, request.pattern);
    const auto nativeSkillInvalid = [&](const QString &name, int instanceId) {
        std::unique_ptr<AIRound56SkillValidityScope> validitySource;
        auto *gameLua=m_room.roomRuntime()->lua().rawState();
        if(request.kind==AIRequest::Activate&&request.reason==CardUseStruct::CARD_USE_REASON_PLAY
            &&!inAIRound56ProjectionScope(m_room,gameLua))
            validitySource.reset(new AIRound56SkillValidityScope(m_room,player,gameLua));
        return player->isSkillInvalid(name,instanceId);
    };
    bool enumerated = true;
    bool staged = !request.hasSkillActionContext;
    int probesLeft = 512;

    if (request.kind == AIRequest::Activate && !scopesOnly) {
    // The UI expands response-accessible hand piles and offers equipment view-as
    // buttons outside Player SkillInstances. This prototype does not ticket those
    // sources. Their absence from the lists must never imply complete coverage.
    if (!player->getHandPile().isEmpty()
        || !player->property("View_As_Equips_List").toString().isEmpty())
        enumerated = false;
    foreach (const Card *equip, player->getEquips()) {
        const auto *realEquip = equip ? qobject_cast<const EquipCard *>(equip->getRealCard()) : nullptr;
        const Skill *equipSkill = realEquip ? Sanguosha->getSkill(realEquip) : nullptr;
        if (equipSkill && ViewAsSkill::parseViewAsSkill(equipSkill)) enumerated = false;
    }

    // V1 view-as skills are not represented by skillActions. Their absence from
    // that list cannot prove that no conversion exists for this response/play.
    for (const SkillInstance &instance : player->getSkillInstances()) {
        if (dynamic_cast<const ViewAsEquipSkill *>(Sanguosha->getSkill(instance.skillName)))
            enumerated = false;
        const ViewAsSkill *skill = Sanguosha->getViewAsSkill(instance.skillName);
        if (skill && !skill->getExpandPile().isEmpty()) enumerated = false;
        if (skill && !dynamic_cast<const ViewAsSkillV2 *>(skill)
            && !nativeSkillInvalid(instance.skillName, instance.instanceID)
            // V1 response availability has separate nullification hooks and legacy
            // pattern conventions. Treat it as unknown without duplicating them.
            && (request.reason != CardUseStruct::CARD_USE_REASON_PLAY
                || skill->aiRound56AvailableForConversion(player, request.reason, request.pattern, [&] {
                    std::unique_ptr<AIRound56WorldSkillQueriesScope> ownershipSource;
                    auto *gameLua=m_room.roomRuntime()->lua().rawState();
                    if(request.kind==AIRequest::Activate&&request.reason==CardUseStruct::CARD_USE_REASON_PLAY
                        &&!inAIRound56ProjectionScope(m_room,gameLua))
                        ownershipSource.reset(new AIRound56WorldSkillQueriesScope(m_room,player,gameLua));
                    return player->hasSkill(skill->objectName());
                })))
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
        const QStringList stages = skill->aiConversionStages();
        if (skill->getN() != 1 || skill->declaresCardName()
            || stages != QStringList({"material", "output", "targets"}))
            staged = false;
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
                staged = false;
                if(produced->isVirtualCard())const_cast<Card *>(produced)->deleteLater();
                continue;
            }
            // These audited producers allocate fresh cards without copying
            // material flags: Chopper's full-hand Slash and FuluVS's single
            // owned Slash -> ThunderSlash (sp.cpp). Never apply completeness
            // to a notified physical product or arbitrary foreign material.
            QJsonObject chopperFlags;
            const bool chopperFlagsKnown = action.getActivationSkillName() == "chopper"
                && cost == 0 && produced->isVirtualCard() && produced->objectName() == "slash"
                && !produced->getSubcards().isEmpty() && produced->getSubcards() == player->handCards();
            const bool fuluFlagsKnown = action.getActivationSkillName() == "fulu"
                && cost == 1 && selection.size() == 1 && produced->isVirtualCard()
                && produced->objectName() == "thunder_slash" && produced->getSkillName() == "fulu"
                && produced->getSubcards() == selection
                && m_room.getCardOwner(selection.first()) == player;
            if (chopperFlagsKnown || fuluFlagsKnown)
                for (const QString &flag : produced->getFlags()) chopperFlags.insert(flag, true);
            const bool round33Physical = aiRound33PlanningPhysicalScope(request,action.getActivationSkillName())
                && produced->isKindOf("EquipCard");
            if (round33Physical && !authorizeAIRound33PlanningPhysicalProduct(request,m_room,player,
                    action.activationRef,action.sourceRef,selection,produced)) {
                if (produced->isVirtualCard()) const_cast<Card *>(produced)->deleteLater();
                continue;
            }
            AICardConversionView view;
            view.conversionId = request.cardConversions.size() + 1;
            view.producerReason = request.reason;
            view.producerPattern = request.pattern;
            view.legacyIdentityToken = aiLegacyCardIdentity(m_room, player, produced);
            view.nameLength = produced->nameLength();
            view.legacyStringKey = produced->toString();
            view.legacySortKey = legacySortKey(produced);
            view.name = produced->objectName();
            view.className = produced->getClassName();
            view.kindOfNames = produced->getKindOfNames();
            view.suit = int(produced->getSuit());
            view.number = produced->getNumber();
            view.typeId = int(produced->getTypeId());
            view.handlingMethod = int(produced->getHandlingMethod());
            view.targetResidue = responseResidue(m_room, player, produced);
            view.cardRules = responseOwnerRules(m_room, player, produced);
            if (round33Physical) view.cardRules.insert("physical_product_id",produced->getEffectiveId());
            const bool ownSkillChain = produced->isVirtualCard() || m_room.getCardOwner(produced->getEffectiveId()) == player;
            view.cardRules.insert("skill_names_complete", ownSkillChain);
            if (ownSkillChain) view.cardRules.insert("skill_names", QJsonArray::fromStringList(produced->getSkillNames()));
            if (chopperFlagsKnown || fuluFlagsKnown) {
                view.cardRules.insert("known_flags", chopperFlags);
                view.cardRules.insert("flags_complete", true);
            }
            view.canRecast = produced->canRecast();
            view.willThrow = produced->willThrow();
            view.locked = player->isLocked(produced);
            view.usingCard = produced->hasFlag("using");
            view.containsJinchantuoqiao = produced->toString().contains(QStringLiteral("jinchantuoqiao"));
            view.activationRef = action.activationRef;
            view.sourceRef = action.sourceRef;
            view.activationQuotaAvailable = action.activationQuotaAvailable;
            view.sourceQuotaAvailable = action.sourceQuotaAvailable;
            const bool zhangwuGenerated = aiRound27ZhangwuGeneratedBranch(request, player, action.getActivationSkillName());
            view.generatedHandPayment = action.getActivationSkillName() == "chopper"
                || action.getActivationSkillName() == "tiaojiaoCMT" || zhangwuGenerated;
            if (view.generatedHandPayment && (cost != 0 || !produced->isVirtualCard()
                || (zhangwuGenerated
                    ? (produced->objectName() != "slash" || produced->getSkillName(false) != "lxtx_zhangwuu"
                        || !aiRound27ZhangwuGeneratedPayment(request, player, action.getActivationSkillName(), produced->getSubcards()))
                    : (produced->objectName() != (action.getActivationSkillName() == "chopper" ? "slash" : "tiaojiaoCMTCard")
                        || (action.getActivationSkillName() == "chopper" && produced->getSubcards().isEmpty())
                        || produced->getSubcards() != player->handCards())))) {
                enumerated = false;
                if (produced->isVirtualCard()) const_cast<Card *>(produced)->deleteLater();
                continue;
            }
            view.subcardIds = view.generatedHandPayment ? produced->getSubcards() : selection;
            view.selectionStages = stages;
            view.costCount = parameterized ? cost : 0;
            view.eligibleSubcardIds = eligible;
            const AICardCandidateView candidate = makeAICardCandidate(m_room, player,
                produced, request.handlingMethod, request.kind, request.pattern, projectionBudget);
            view.available = candidate.available && !candidate.limited;
            view.matchesResponsePattern = candidate.available;
            view.extraTargetKnown = candidate.extraTargetKnown;
            view.extraTargetCount = candidate.extraTargetCount;
            view.extraTargetByPlayer = candidate.extraTargetByPlayer;
            view.targetFilterEmpty = candidate.targetFilterEmpty;
            view.skillName = produced->getSkillName(false);
            view.damageCard = produced->isDamageCard(); view.singleTarget = produced->isSingleTargetCard();
            view.red = produced->isRed(); view.black = produced->isBlack();
            if (!view.available) { if(produced->isVirtualCard())const_cast<Card *>(produced)->deleteLater(); continue; }
            view.targetFixed = candidate.targetFixed;
            view.feasibleWithNoTarget = candidate.feasibleWithNoTarget;
            view.completeCoverage = candidate.completeCoverage;
            view.legalTargets = candidate.legalTargets;
            view.maxVotes = candidate.maxVotes;
            view.targetCombinations = candidate.targetCombinations;
            view.affectedTargetsKnown = candidate.affectedTargetsKnown;
            view.affectedTargets = candidate.affectedTargets;
            // The Lua projection remains a value. Only the two audited saved-plan
            // source questions retain native-only original producer custody.
            if(needsOlClanObservationCustody(request,player)) {
                AIReadonlyCardLease lease=produced->isVirtualCard()
                    ? AIOwnerReadonlyCardStore::own(std::unique_ptr<Card>(const_cast<Card *>(produced)))
                    : AIOwnerReadonlyCardStore::observe(produced);
                if(lease){
                    request.conversionLedger->observedProducts.insert(view.conversionId,std::move(lease));
                    m_room.roomRuntime()->ai().trackObservationLedger(request.conversionLedger);
                }
            } else if(produced->isVirtualCard())const_cast<Card *>(produced)->deleteLater();
            request.cardConversions << view;
        }
    }
    }
    // Pure catalogue metadata, never a legacy AI strategy. Keep the actual
    // patterns() closure table: rebuilding a table changes lua_next discovery.
    lua_State *metadataState = m_room.roomRuntime()->lua().state();
    const int metadataTop = lua_gettop(metadataState);
    QJsonArray classOrder, physicalClassOrder;
    QJsonObject cardCatalog, byClass, scopes;
    QList<QPair<QString, QString>> classes;
    bool catalogComplete = false;
    lua_getglobal(metadataState, "patterns");
    if (lua_isfunction(metadataState, -1) && !lua_iscfunction(metadataState, -1)) {
        lua_pushvalue(metadataState, -1);
        lua_Debug definition{};
        const bool sourceKnown = lua_getinfo(metadataState, ">S", &definition);
        const QString definitionSource = QString::fromUtf8(definition.source ? definition.source : "");
        const bool corePatterns = definitionSource.endsWith("/ai/smart-ai.lua") && definition.linedefined == 33;
        // addFunction loads first at the pinned extensions SHA, so its equivalent
        // public-only catalogue closure normally prevents SmartAI's guarded copy.
        const bool extensionPatterns = (definitionSource.endsWith("/extensions/addFunction.lua")
            || definitionSource == "@extensions/addFunction.lua") && definition.linedefined == 1809;
        if (sourceKnown && (corePatterns || extensionPatterns)) {
            lua_pushboolean(metadataState, true);
            AIRound56PatternsScope patternsSource(m_room, metadataState);
            if (LuaRuntime::protectedCall(metadataState, 1, 1, 0) == 0
                && lua_istable(metadataState, -1)) {
                catalogComplete = true;
                lua_pushnil(metadataState);
                while (lua_next(metadataState, -2)) {
                    if (!lua_isstring(metadataState, -2) || !lua_isstring(metadataState, -1)) {
                        catalogComplete = false;
                        lua_pop(metadataState, 1);
                        continue;
                    }
                    classes << qMakePair(QString::fromUtf8(lua_tostring(metadataState, -2)),
                                         QString::fromUtf8(lua_tostring(metadataState, -1)));
                    lua_pop(metadataState, 1);
                }
            }
        }
    }
    lua_settop(metadataState, metadataTop);
    const int catalogClassCount = classes.size();
    QString actualPattern = request.pattern;
    if (actualPattern.endsWith('!')) actualPattern.chop(1);
    QStringList seenPhysical;
    bool physicalClassOrderComplete = true;
    QString cardSource = QStringLiteral("he");
    const QStringList placePattern = actualPattern.split('|');
    if (placePattern.size() >= 4) {
        const QStringList places = placePattern.at(3).split(',');
        if (places.size() == 1) {
            if (places.first() == "hand") cardSource = QStringLiteral("h");
            else if (places.first() == "equipped") cardSource = QStringLiteral("e");
        }
    }
    // smart-ai3839 discovers classes before any availability/limit filtering.
    // addHandPile appends hand piles even to an equipped-only source list.
    QList<const Card *> discoveryCards = player->getCards(cardSource);
    for (int id : player->getHandPile()) discoveryCards << Sanguosha->getCard(id);
    for (const Card *card : discoveryCards) {
        if (!card) { physicalClassOrderComplete = false; continue; }
        const QString className = card->isKindOf("Slash") ? QStringLiteral("Slash") : card->getClassName();
        if (!seenPhysical.contains(className) && (actualPattern.contains(card->objectName())
            || Sanguosha->matchPattern(actualPattern, player, card))) {
            seenPhysical << className;
            physicalClassOrder.append(className);
        }
    }
    QList<int> owned = player->handCards();
    for (const Card *card : player->getEquips()) if (card) owned << card->getEffectiveId();
    for (int id : player->getHandPile()) if (!owned.contains(id)) owned << id;
    for (int id : owned) {
        const Card *card = Sanguosha->getCard(id);
        if (!card) continue;
        const QString className = card->isKindOf("Slash") ? QStringLiteral("Slash") : card->getClassName();
        bool found = false;
        for (const auto &entry : classes) if (entry.first == className) { found = true; break; }
        if (!found) classes << qMakePair(className, card->objectName());
    }
    for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
        const Card *card = Sanguosha->getEngineCard(id);
        if (card) {
            cardCatalog.insert(card->objectName(), QJsonObject{
                {"kind_of_names", QJsonArray::fromStringList(card->getKindOfNames())}});
            for (const QString &kind : card->getKindOfNames()) {
                bool found = false;
                for (const auto &entry : classes) if (entry.first == kind) { found = true; break; }
                if (!found) classes << qMakePair(kind, kind); // patterns(string) falls back to its argument.
            }
        }
    }
    // This is the native sequence used by getPlayerSkillList before its cache;
    // no private opponent skills are inspected, including weidi's lord suffix.
    QList<const Skill *> skillList;
    for (const Skill *skill : player->getSkillList(true)) {
        if (skill && (skill->isLordSkill() ? ([&] { std::unique_ptr<AIRound56WorldSkillQueriesScope> source;
        auto *gameLua=m_room.roomRuntime()->lua().rawState();
        if(request.kind==AIRequest::Activate&&request.reason==CardUseStruct::CARD_USE_REASON_PLAY&&!inAIRound56ProjectionScope(m_room,gameLua))
            source.reset(new AIRound56WorldSkillQueriesScope(m_room,player,gameLua));
        return player->hasLordSkill(skill->objectName()); }()) : ([&] { std::unique_ptr<AIRound56WorldSkillQueriesScope> source;
        auto *gameLua=m_room.roomRuntime()->lua().rawState();
        if(request.kind==AIRequest::Activate&&request.reason==CardUseStruct::CARD_USE_REASON_PLAY&&!inAIRound56ProjectionScope(m_room,gameLua))
            source.reset(new AIRound56WorldSkillQueriesScope(m_room,player,gameLua));
        return player->hasSkill(skill); }())))
            skillList << skill;
    }
    bool rosterComplete = true;
    if (([&] { std::unique_ptr<AIRound56WorldSkillQueriesScope> source;
        auto *gameLua=m_room.roomRuntime()->lua().rawState();
        if(request.kind==AIRequest::Activate&&request.reason==CardUseStruct::CARD_USE_REASON_PLAY&&!inAIRound56ProjectionScope(m_room,gameLua))
            source.reset(new AIRound56WorldSkillQueriesScope(m_room,player,gameLua));
        return player->hasSkill("weidi"); }())) {
        const ServerPlayer *lord = m_room.getLord();
        if (lord && lord != player) {
            if (ServerInfo.EnableHegemony && !lord->hasShownAllGenerals() && lord->isAlive())
                rosterComplete = false;
            else for (const Skill *skill : lord->getSkillList(true))
                if (skill && skill->isLordSkill()) skillList << skill;
        }
    }
    int nextScope = 1;
    lua_getglobal(metadataState, "sgs");
    const int metadataSgs = lua_gettop(metadataState);
    bool policyComplete = catalogComplete && rosterComplete;
    // assignKeep copies every global and owner-skill keep-value key. A key
    // need not be a currently decked prototype (e.g. Wangmeizhike), and its
    // response callback presence must still be described authoritatively.
    // Appending after catalogClassCount preserves patterns() discovery/RNG order.
    const auto appendKeepClasses = [&](const QByteArray &tableName) {
        lua_pushlstring(metadataState, tableName.constData(), size_t(tableName.size()));
        lua_rawget(metadataState, metadataSgs);
        if (lua_istable(metadataState, -1)) {
            lua_pushnil(metadataState);
            while (lua_next(metadataState, -2)) {
                if (lua_type(metadataState, -2) == LUA_TSTRING) {
                    const QString key = QString::fromUtf8(lua_tostring(metadataState, -2));
                    bool found = false;
                    for (const auto &entry : classes) if (entry.first == key) { found = true; break; }
                    if (!found) classes << qMakePair(key, key);
                }
                lua_pop(metadataState, 1);
            }
        }
        lua_pop(metadataState, 1);
    };
    if (lua_istable(metadataState, metadataSgs)) {
        appendKeepClasses("ai_keep_value");
        for (const Skill *skill : skillList)
            appendKeepClasses(skill->objectName().toUtf8() + "_keep_value");
    } else policyComplete = false;
    if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")) {
        std::fprintf(stderr,"R56_CONVERSION_CATALOG_SOURCE classes=%d patterns_classes=%d skills=%d\n",
            int(classes.size()),catalogClassCount,int(skillList.size()));std::fflush(stderr);
    }
    int classIndex = 0;
    for (const auto &entry : classes) {
        const bool discoveredByPatterns = classIndex++ < catalogClassCount;
        const QString className = entry.first, cardName = entry.second;
        Card *dummy = Sanguosha->cloneCard(cardName);
        if (dummy) {
            if (discoveredByPatterns && (actualPattern.contains(cardName)
                || Sanguosha->matchExpPattern(actualPattern, player, dummy))) classOrder.append(className);
            dummy->deleteLater();
        }
        QString enabledPattern = cardName;
        if ((className == "Peach" || className == "Analeptic") && m_room.getCurrentDyingPlayer() == player)
            enabledPattern = QStringLiteral("peach+analeptic");
        QJsonArray rows;
        bool classComplete = rosterComplete;
        for (const Skill *nativeSkill : skillList) {
            const QString skillName = nativeSkill->objectName();
            const ViewAsSkill *vs = ViewAsSkill::parseViewAsSkill(nativeSkill);
            const auto *v2 = dynamic_cast<const ViewAsSkillV2 *>(vs);
            QJsonObject hooks, hookKeys;
            bool hooksKnown = lua_istable(metadataState, metadataSgs);
            for (const QString &registry : {"ai_guhuo_card", "ai_cardsview_valuable", "ai_cardsview", "ai_view_as"}) {
                bool present = false;
                if (hooksKnown) {
                    lua_getfield(metadataState, metadataSgs, registry.toUtf8().constData());
                    if (!lua_istable(metadataState, -1)) hooksKnown = false;
                    else {
                        lua_getfield(metadataState, -1, skillName.toUtf8().constData());
                        present = lua_isfunction(metadataState, -1);
                        lua_pop(metadataState, 1);
                    }
                    lua_pop(metadataState, 1);
                }
                if (hooksKnown) {
                    hooks.insert(registry, present);
                    if (present) hookKeys.insert(registry, skillName);
                }
            }
            if (!hooksKnown) classComplete = false;
            AiSkillActionContext action;
            CardUseStruct::CardUseReason producerReason = request.reason;
            QString producerPattern = request.pattern.isEmpty() ? enabledPattern : request.pattern;
            Card::HandlingMethod producerMethod = request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                ? Card::MethodResponse : Card::MethodUse;
            bool hasAction = false;
            QList<QPair<CardUseStruct::CardUseReason, QString>> contexts;
            if (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
                contexts << qMakePair(producerReason, producerPattern);
            else contexts << qMakePair(CardUseStruct::CARD_USE_REASON_RESPONSE, enabledPattern)
                          << qMakePair(CardUseStruct::CARD_USE_REASON_RESPONSE_USE, enabledPattern);
            if (v2) for (const auto &context : contexts) {
                for (const SkillInstance &instance : player->getSkillInstances()) {
                    if (instance.skillName == skillName
                        && buildSkillActionContext(player, instance, context.first, context.second, action)) {
                        producerReason = context.first; producerPattern = context.second;
                        producerMethod = producerReason == CardUseStruct::CARD_USE_REASON_RESPONSE
                            ? Card::MethodResponse : Card::MethodUse;
                        hasAction = true; break;
                    }
                }
                if (hasAction) break;
            }
            if (!hasAction) {
                for (const SkillInstance &instance : player->getSkillInstances()) {
                    if (instance.skillName != skillName || nativeSkillInvalid(skillName, instance.instanceID)) continue;
                    action.activationRef = SkillInstanceRef(player->objectName(), instance.key());
                    action.sourceRef = m_skillRuntime.resolveSkillInstanceRootRef(action.activationRef);
                    if (!action.sourceRef.isValid()) action.sourceRef = action.activationRef;
                    action.activationQuotaAvailable = action.sourceQuotaAvailable = true;
                    break;
                }
            }
            if (hasAction && hooksKnown && action.sourceRef.key.skillName != skillName) {
                for (const QString &registry : {"ai_cardsview_valuable", "ai_cardsview"}) {
                    if (hooks.value(registry).toBool()) continue;
                    lua_getfield(metadataState, metadataSgs, registry.toUtf8().constData());
                    lua_getfield(metadataState, -1, action.sourceRef.key.skillName.toUtf8().constData());
                    const bool present = lua_isfunction(metadataState, -1);
                    hooks.insert(registry, present);
                    if (present) hookKeys.insert(registry, action.sourceRef.key.skillName);
                    lua_pop(metadataState, 2);
                }
            }
            const bool enabled = vs && vs->isEnabledAtResponse(player, enabledPattern);
            QJsonObject identity = responseIdentity(action);
            QJsonObject row{{"skill_name", skillName}, {"view_as_exists", vs != nullptr},
                {"enabled_at_response", enabled}, {"request", false}, {"identity", identity},
                {"legacy_hooks", hooks}, {"legacy_hook_keys", hookKeys}, {"conversion_ids", QJsonArray()}, {"class_name", className}};
            if (hasAction) row.insert("request", QJsonObject{{"reason", int(producerReason)},
                {"pattern", producerPattern}, {"prompt", ""}, {"handling_method", int(producerMethod)},
                {"skill_action", identity}});
            if (vs && (enabled || hasAction)) {
                if (!action.activationRef.isValid() || !action.sourceRef.isValid()) classComplete = false;
                else {
                    const int scopeId = nextScope++;
                    row.insert("scope_id", scopeId);
                    row.insert("lazy", true);
                    scopes.insert(QString::number(scopeId), QJsonObject{{"class_name", className},
                        {"card_name", cardName}, {"skill_name", skillName}, {"identity", identity},
                        {"version", v2 ? 2 : 1}, {"reason", int(producerReason)},
                        {"pattern", producerPattern}, {"enabled_pattern", enabledPattern},
                        {"handling_method", int(producerMethod)}, {"has_action", hasAction},
                        {"legacy_hook_keys", hookKeys}});
                    // Only cardsView attaches an action request. Guhuo and per-card
                    // view_as execute in the caller's unchanged response context and
                    // preserve the callback's own skill attribution.
                    if (hasAction && enabled) {
                        const int directScopeId = nextScope++;
                        row.insert("direct_scope_id", directScopeId);
                        QJsonObject direct = scopes.value(QString::number(scopeId)).toObject();
                        direct.insert("reason", int(request.reason));
                        direct.insert("pattern", request.pattern);
                        direct.insert("handling_method", int(request.handlingMethod));
                        direct.insert("has_action", false);
                        scopes.insert(QString::number(directScopeId), direct);
                    }
                }
            }
            // A turn fill must never borrow a response-authorized receipt. Keep
            // play authority separate even when both paths name the same skill.
            if (request.kind == AIRequest::Activate
                && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && vs) {
                AiSkillActionContext playAction;
                bool playEnabled = false;
                for (const SkillInstance &instance : player->getSkillInstances()) {
                    if (instance.skillName != skillName || nativeSkillInvalid(skillName, instance.instanceID)) continue;
                    if (v2) {
                        if (!buildSkillActionContext(player, instance, request.reason, request.pattern, playAction)) continue;
                    } else {
                        if (!vs->isEnabledAtPlay(player)) continue;
                        playAction.activationRef = SkillInstanceRef(player->objectName(), instance.key());
                        playAction.sourceRef = m_skillRuntime.resolveSkillInstanceRootRef(playAction.activationRef);
                        if (!playAction.sourceRef.isValid()) playAction.sourceRef = playAction.activationRef;
                        playAction.activationQuotaAvailable = playAction.sourceQuotaAvailable = true;
                    }
                    playEnabled = true;
                    break;
                }
                if (playEnabled) {
                    const int playScopeId = nextScope++;
                    const QJsonObject playIdentity = responseIdentity(playAction);
                    row.insert("play_scope_id", playScopeId);
                    row.insert("play_identity", playIdentity);
                    row.insert("play_request", QJsonObject{{"viewer", player->objectName()}, {"reason", int(request.reason)},
                        {"pattern", request.pattern}, {"prompt", request.prompt},
                        {"handling_method", int(Card::MethodUse)}, {"skill_action", playIdentity}});
                    scopes.insert(QString::number(playScopeId), QJsonObject{{"class_name", className},
                        {"card_name", cardName}, {"skill_name", skillName}, {"identity", playIdentity},
                        {"version", v2 ? 2 : 1}, {"reason", int(request.reason)}, {"pattern", request.pattern},
                        {"handling_method", int(Card::MethodUse)}, {"has_action", v2 != nullptr}, {"play", true}});
                }
            }
            rows.append(row);
        }
        byClass.insert(className, QJsonObject{{"complete", classComplete}, {"card_name", cardName},
            {"skills", rows}, {"context", QJsonObject{{"reason", int(request.reason)},
                {"pattern", request.pattern}, {"prompt", request.prompt}}}});
        policyComplete &= classComplete;
    }
    // Native skill scopes do not require a guessed output name. Only this
    // request's live V2 instances can issue such a receipt, including nameless
    // ActiveSkillCard proxies and variable-cost actions.
    QJsonArray skillActions;
    for (const SkillInstance &instance : player->getSkillInstances()) {
        // ThWeijing's finite native inquiry names a V1 activation, not a
        // printed-card expression. Keep every other Zhifeng entry unchanged.
        if (instance.skillName == "zhifeng"
            && (request.kind != AIRequest::UseCard
                || request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                || request.pattern != "@@zhifeng" || request.prompt != "zhifeng0"
                || request.handlingMethod != Card::MethodUse)) continue;
        if ((instance.skillName == "ol2shanjia" || instance.skillName == "jiewan")
            && !aiRound52OlspQuestion(request, player)) continue;
        AiSkillActionContext action;
        if (nativeSkillInvalid(instance.skillName, instance.instanceID)
            || !buildSkillActionContext(player, instance, request.reason, request.pattern, action)) continue;
        const int scopeId = nextScope++;
        const QJsonObject identity = responseIdentity(action);
        const QJsonObject actionRequest{{"viewer", player->objectName()}, {"reason", int(request.reason)},
            {"pattern", request.pattern}, {"prompt", request.prompt},
            {"handling_method", int(request.handlingMethod)}, {"skill_action", identity}};
        skillActions.append(QJsonObject{{"skill_name", instance.skillName}, {"identity", identity},
            {"scope_id", scopeId}, {"request", actionRequest}});
        scopes.insert(QString::number(scopeId), QJsonObject{{"class_name", "@skill_action"},
            {"skill_name", instance.skillName}, {"identity", identity}, {"version", dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill(instance.skillName)) ? 2 : 1},
            {"reason", int(request.reason)}, {"pattern", request.pattern},
            {"handling_method", int(request.handlingMethod)}, {"has_action", true}, {"skill_action", true}});
    }
    lua_settop(metadataState, metadataTop);
    request.conversionPolicy = QJsonObject{{"complete", policyComplete}, {"lazy", true},
        {"class_order", classOrder}, {"class_order_complete", catalogComplete},
        {"physical_class_order", physicalClassOrder}, {"physical_class_order_complete", physicalClassOrderComplete},
        {"by_class", byClass},
        {"scopes", scopes}, {"card_catalog", cardCatalog}, {"skill_actions", skillActions}};
    if (request.kind != AIRequest::Activate) enumerated = policyComplete;
    request.conversionsEnumerated = !scopesOnly && enumerated;
    request.stagedActionsComplete = !scopesOnly && enumerated && staged;
    return enumerated;

}

// Fixed olwenwu.cpp:776-875: a zero-selection native product carries a
// material tag; cost/pay binds that same sole physical card later. This is not
// a generic permission to turn selected materials into zero-cost cards.
static bool round25JincaiwangMaterial(const AIRequest &request, Room &room,
    ServerPlayer *player, const QString &name, const QList<int> &ids)
{
    if (!player || request.viewerObjectName != player->objectName() || ids.size() != 1
        || request.stateRevision != room.roomRuntime()->stateRevision()) return false;
    QList<const Card *> zone;
    Player::Place place = Player::PlaceUnknown;
    if (name == "slash") { zone = player->getJudgingArea(); place = Player::PlaceDelayedTrick; }
    else if (name == "jink") { zone = player->getHandcards(); place = Player::PlaceHand; }
    else if (name == "nullification") { zone = player->getEquips(); place = Player::PlaceEquip; }
    else return false;
    if (zone.size() != 1 || !zone.first() || zone.first()->getEffectiveId() != ids.first()
        || zone.first()->hasFlag("using") || room.getCardOwner(ids.first()) != player
        || room.getCardPlace(ids.first()) != place) return false;
    bool disclosed = false;
    const auto includes = [&](const QList<AICardView> &cards) {
        for (const auto &card : cards) if (card.effectiveId == ids.first()) return true;
        return false;
    };
    if (place == Player::PlaceHand) disclosed = includes(request.worldView.handCards);
    else if (place == Player::PlaceEquip) disclosed = includes(request.worldView.self.equips);
    else disclosed = includes(request.worldView.self.judgingArea);
    return disclosed;
}
static bool round25JincaiwangConstruction(const AIRequest &r, ServerPlayer *p, const QJsonObject &spec)
{
    if (!p || !p->getRoom() || !p->hasSkill("secondjincaiwang") || r.kind != AIRequest::Activate
        || r.reason != CardUseStruct::CARD_USE_REASON_PLAY || spec.value("source_key") != "secondjincaiwang"
        || spec.value("skill") != "secondjincaiwang" || spec.value("name") != "slash"
        || !spec.value("subcards").isArray() || spec.value("subcards").toArray().size() != 1) return false;
    const auto value = spec.value("subcards").toArray().first();
    const int id = value.toInt(-1);
    if (id < 0 || value != id || !round25JincaiwangMaterial(r,*p->getRoom(),p,"slash",{id})) return false;
    const Card *card = Sanguosha->getCard(id);
    return card && spec.value("suit") == int(card->getSuit()) && spec.value("number") == card->getNumber();
}
static bool round25JincaiwangProduct(const Card *card, const QString &name,
    const QList<int> &ids)
{
    if (!card || ids.size() != 1 || !card->isVirtualCard() || !card->getSubcards().isEmpty()
        || card->objectName() != name || card->getSkillName(false) != "secondjincaiwang") return false;
    const auto material = card->getTag("jincaiwang_material").toMap();
    bool ok = false; const int id = material.value("id").toInt(&ok);
    return ok && material.value("name").toString() == name && id == ids.first();
}

QJsonObject AiDecisionCoordinator::resolveConversionProposal(const AIRequest &parentRequest,
                                                              const QJsonObject &proposal) const
{
    // The inquiry scope is not authority for arbitrary native RPCs. Resolve
    // only the complete finite readonly source family below while it is live;
    // every other host entry retires BEFORE native constructors or callbacks.
    auto*round56Lua=m_room.roomRuntime()->lua().rawState();
    if(inAIRound56ProjectionScope(m_room,round56Lua)){
        const QString kind=proposal.value("kind").toString();
        const auto spec=proposal.value("spec").toObject();
        auto*owner=m_room.findPlayerByObjectName(parentRequest.viewerObjectName,true);
        bool proved=kind=="ordinary_card_choice_prefix";
        if(kind=="evaluate_owner_view_as")
            proved=parentRequest.kind==AIRequest::PlayerChosen&&parentRequest.choiceOptions.reason=="choulve"
                &&spec.value("source_key")=="choulve"&&Sanguosha->aiRound56StandardCloneFactory(spec.value("name").toString());
        if(kind=="card_target_filter"&&proposal.value("recipe_key").isString()){
            const Card*c=m_room.roomRuntime()->ai().readonlyCard(parentRequest,owner,proposal.value("recipe_key").toString());
            proved=c&&preserveAIRound56ProjectionNativeQuery(m_room,c,true);
        }
        if(kind=="evaluate_owner_range_card"){
            proved=(spec.size()==1&&(spec.value("session_current")==true||spec.value("owner_state")==true));
            if(spec.value("recipe_key").isString()){
                const Card*c=m_room.roomRuntime()->ai().readonlyCard(parentRequest,owner,spec.value("recipe_key").toString());
                proved=c&&preserveAIRound56ProjectionNativeQuery(m_room,c,true);
            }else if(spec.value("name").isString())
                proved=Sanguosha->aiRound56StandardCloneFactory(spec.value("name").toString());
        }
        if(!proved){
            if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")){
                std::fprintf(stderr,"R56_QUERY_RETIRE site=conversion-proposal-gate\n");std::fflush(stderr);
            }
            retireAIRound56Projection(m_room,round56Lua);
        }
    }
    if(proposal.value("kind")=="select_sijyu_fengyi_physical") {
        const auto id=proposal.value("card_id");
        if(proposal.size()!=2||!id.isDouble()||id.toDouble()<0||id.toDouble()!=id.toInt(-1))
            return {{"error","invalid Fengyi physical selection"},{"native_rejected",true}};
        EngineRuntimeContextScope engineContext(*Sanguosha,&m_room);
        LuaRuntime::Binding game(m_room.roomRuntime()->lua(),false);
        auto *viewer=m_room.findPlayerByObjectName(parentRequest.viewerObjectName,true);
        return selectAIRound47FengyiPhysical(m_room,parentRequest,viewer,id.toInt());
    }
    if(proposal.value("kind")=="rushb_physical_skill"||proposal.value("kind")=="rushb_physical_skill_read") {
        QString error;auto *viewer=m_room.findPlayerByObjectName(parentRequest.viewerObjectName,true);
        EngineRuntimeContextScope engineContext(*Sanguosha,&m_room);
        LuaRuntime::Binding gameplay(m_room.roomRuntime()->lua(),false);
        const Card *card=writeAIRound45PhysicalSkill(m_room,parentRequest,viewer,proposal,&error);
        if(!card)return {{"error",error},{"key","rushB.physical_mutation"}};
        // Same physical source object, with new native rules evaluated AFTER
        // the source write. This object is neither a readonly clone nor receipt.
        auto row=publicDecisionCard(card,true);
        row.insert("evaluation_only",false);row.insert("virtual_card",false);
        row.insert("effective_id",card->getEffectiveId());row.insert("subcards",responseIds(card->getSubcards()));
        row.insert("evaluation_owner",viewer->objectName());
        row.insert("evaluation_rules",responseOwnerRules(m_room,viewer,card));
        row.insert("target_residue",responseResidueJson(responseResidue(m_room,viewer,card)));
        row.insert("can_recast",card->canRecast());row.insert("target_fixed",card->targetFixed());
        row.insert("handling_method",int(card->getHandlingMethod()));
        row.insert("legacy_string_key",card->toString());row.insert("legacy_sort_key",legacySortKey(card));
        row.insert("locked",viewer->isLocked(card));row.insert("jilei",viewer->isJilei(card));
        row.insert("legacy_identity_token",aiLegacyCardIdentity(m_room,viewer,card));
        int budget=4096;
        const auto candidate=responseCandidateJson(makeAICardCandidate(m_room,viewer,card,
            parentRequest.handlingMethod,parentRequest.kind,parentRequest.pattern,budget));
        if(!aiRound44RangeRequestCurrent(m_room,parentRequest,viewer))return {{"error","RushB post-write projection changed state"},{"key","rushB.physical_mutation"}};
        return {{"kind","rushb_physical_skill"},{"viewer",viewer->objectName()},
            {"decision_id",parentRequest.getDecisionId()},{"state_revision",parentRequest.getStateRevision()},
            {"card",row},{"candidate",candidate}};
    }
    if (proposal.value("kind")=="overseas_priority")
        return queryAIRound29ChoicePriority(m_room,parentRequest,m_room.findPlayerByObjectName(parentRequest.viewerObjectName,true),proposal);
    if (proposal.value("kind")=="overseas_xiafeng_flag") {
        const auto id=proposal.value("card_id");
        if(proposal.size()!=3||!id.isDouble()||id.toDouble()<0||id.toDouble()!=id.toInt(-1)||!proposal.value("enabled").isBool())
            return {{"error","invalid Xiafeng flag proposal"}};
        return writeAIRound29XiafengFlag(m_room,parentRequest,m_room.findPlayerByObjectName(parentRequest.viewerObjectName,true),id.toInt(),proposal.value("enabled").toBool());
    }
    // This terminal consumes the original response scope; Play hypotheses can
    // never turn their scopes into payment. The copied request shares only the
    // original ledger and returns through ordinary live provider validation.
    if (proposal.value("kind")=="bind_overseas_niwo_selected") {
        auto fail=[](const char *why){return QJsonObject{{"error",why},{"key","ov_niwo.selected"}};};
        ServerPlayer *viewer=m_room.findPlayerByObjectName(parentRequest.viewerObjectName);
        const auto nested=parentRequest.worldView.strategyFacts.value("readonly_play_context").toObject();
        if(proposal.size()!=2 || !proposal.value("subcards").isArray()
            || !aiRound28NiwoResponseQuestion(parentRequest,m_room,viewer)
            || nested.value("source")!="OverseasVersion.ov_niwo"
            || parentRequest.readonlyParentConversionPolicy.isEmpty())
            return fail("Niwo response is not the current preserved parent question");
        const auto costs=proposal.value("subcards").toArray();
        if(costs.size()>998)return fail("Niwo source payment count is invalid");
        QSet<int> seen;
        for(const auto &value:costs){
            const double n=value.toDouble(-1);
            if(!value.isDouble()||n<0||n!=std::floor(n)||n>std::numeric_limits<int>::max()||seen.contains(int(n)))
                return fail("Niwo source materials are not distinct physical IDs");
            seen.insert(int(n));
        }
        QJsonObject selected;
        for(const auto &v:parentRequest.readonlyParentConversionPolicy.value("skill_actions").toArray()){
            const auto row=v.toObject();const auto identity=row.value("identity").toObject();
            if(row.value("skill_name")!="ov_niwo"||identity.value("activation_skill")!="ov_niwo"
                ||identity.value("source_skill")!="ov_niwo"||identity.value("activation_owner")!=parentRequest.viewerObjectName
                ||identity.value("source_owner")!=parentRequest.viewerObjectName)continue;
            if(!selected.isEmpty())return fail("Niwo parent source is ambiguous");
            selected=row;
        }
        if(selected.isEmpty())return fail("Niwo parent source was not issued");
        AIRequest original=parentRequest;
        original.conversionPolicy=parentRequest.readonlyParentConversionPolicy;
        original.skillActions=parentRequest.readonlyParentSkillActions;
        original.worldView.strategyFacts.remove("readonly_play_context");
        original.readonlyParentConversionPolicy={};original.readonlyParentSkillActions.clear();
        return resolveConversionProposal(original,{{"kind","skill_action"},{"class_name","@skill_action"},
            {"scope_id",selected.value("scope_id")},{"spec",QJsonObject{{"subcards",costs},{"legacy_class","#ov_niwoCard"}}}});
    }
    ServerPlayer *planningViewer = m_room.findPlayerByObjectName(parentRequest.viewerObjectName);
    const bool nestedPlay = aiRound23OlClanPlanningQuestion(parentRequest, m_room, planningViewer)
        || aiRound27ChoicePlanningQuestion(parentRequest, m_room, planningViewer)
        || (parentRequest.worldView.strategyFacts.contains("readonly_play_context")
            && aiRound28ChoicePlanningQuestion(parentRequest, m_room, planningViewer));
    const bool hasNestedContext = parentRequest.worldView.strategyFacts.contains("readonly_play_context");
    if (hasNestedContext && !nestedPlay)
        return {{"error", "nested Play question is stale or no longer active"}, {"key", "zuheta.planning"}};
    if (nestedPlay) {
        static const QSet<QString> readonlyKinds{
            "evaluate_owner_range_card", "evaluate_owner_view_as", "evaluate_skill_card",
            "evaluate_skill_seed", "evaluate_tianyi_slash", "evaluate_tongyi_policy",
            "evaluate_zenhui_card", "evaluate_notified_card", "evaluate_nengchen_preview",
            "match_owner_discard_pattern", "inspect_nested_cardask_string", "card_target_filter",
            "qiuxin_target_filter", "evaluate_response_policy", "response_catalogue", "pattern_log_name", "deck_count_shuffle",
            "random_own_hand", "random_hand_discard", "refresh_discard_preferences", "capture_olclan_plan", "allocate_olclan_policy_card", "olclan_saved_slot"};
        if (!hasNestedContext || !readonlyKinds.contains(proposal.value("kind").toString()))
            return {{"error", "nested Play planning cannot issue payment authority"}, {"key", "zuheta.planning"}};
    }
    std::unique_ptr<AIRequest> effectiveRequest;
    if (nestedPlay) {
        effectiveRequest.reset(new AIRequest(parentRequest));
        effectiveRequest->kind = AIRequest::Activate;
        effectiveRequest->reason = CardUseStruct::CARD_USE_REASON_PLAY;
        effectiveRequest->pattern.clear(); effectiveRequest->prompt.clear();
        effectiveRequest->handlingMethod = Card::MethodUse;
    }
    const AIRequest &request = effectiveRequest ? *effectiveRequest : parentRequest;
    std::unique_ptr<AIRound44RangeRequestAlias> rangeReadonlyAlias;
    if(effectiveRequest)rangeReadonlyAlias.reset(new AIRound44RangeRequestAlias(parentRequest,*effectiveRequest));
    // Runtime recipe identity is always the real active inquiry address, never
    // effectiveRequest. The copy is only native constructor/query context.
    std::unique_ptr<ResponseNativeScope> nestedScope;
    if (nestedPlay) nestedScope.reset(new ResponseNativeScope(m_room, planningViewer,
        CardUseStruct::CARD_USE_REASON_PLAY, QString()));
    AIIdentityProjectionScope identityScope(m_room, m_room.findPlayerByObjectName(request.viewerObjectName));
    if (AIOwnerRangeQueryScope::isActive()) {
        static const QSet<QString> readonlyKinds{
            "evaluate_owner_range_card", "qiuxin_target_filter", "evaluate_owner_view_as", "evaluate_skill_card",
            "evaluate_skill_seed", "evaluate_tianyi_slash", "evaluate_tongyi_policy",
            "evaluate_zenhui_card", "evaluate_notified_card", "evaluate_nengchen_preview",
            "match_owner_discard_pattern", "inspect_nested_cardask_string", "card_target_filter",
            "evaluate_response_policy", "response_catalogue", "pattern_log_name", "deck_count_shuffle", "random_own_hand",
            "random_hand_discard", "refresh_discard_preferences", "allocate_olclan_policy_card", "olclan_saved_slot"};
        if (!readonlyKinds.contains(proposal.value("kind").toString()))
            return {{"error", "owner range hypothesis cannot issue payment authority"},
                    {"key", "owner_range"}};
    }
    if(proposal.value("kind")=="xuanzhu_actual_slot") {
        auto fail=[](const QString &why){return QJsonObject{{"error",why},{"key","xuanzhuUse"}};};
        auto *player=m_room.findPlayerByObjectName(request.viewerObjectName);
        if(!player || !aiRound44RangeRequestCurrent(m_room,parentRequest))return fail("Xuanzhu active request is stale");
        EngineRuntimeContextScope engine(*Sanguosha,&m_room);
        LuaRuntime::Binding game(m_room.roomRuntime()->lua(),false);
        ResponseNativeScope native(m_room,player,request.reason,request.pattern);
        const QString mode=proposal.value("mode").toString();QString error;
        if(mode=="capture") {
            if(proposal.size()!=5||!proposal.value("selector").isObject()||!proposal.value("targets").isArray()||!proposal.value("source_fields").isObject())return fail("Xuanzhu capture shape malformed");
            QStringList names;for(const auto &v:proposal.value("targets").toArray()){if(!v.isString()||v.toString().isEmpty())return fail("Xuanzhu target malformed");names<<v.toString();}
            if(names.size()>128)return fail("Xuanzhu target budget exceeded");
            const auto selector=proposal.value("selector").toObject();
            AIReadonlyCardLease lease;
            if (selector.size() == 1 && selector.value("readonly_recipe_key").isString()) {
                lease = m_room.roomRuntime()->ai().retainReadonlyCard(parentRequest,player,
                    selector.value("readonly_recipe_key").toString());
            } else if (selector.size() == 2 && selector.value("legacy_identity_token").isString()) {
                const QString identity = selector.value("legacy_identity_token").toString();
                const bool conversion = selector.contains("conversion_id");
                const auto value = selector.value(conversion ? "conversion_id" : "physical_id");
                const double raw = value.toDouble(-1);
                if (!value.isDouble() || raw < 0 || raw != std::floor(raw)
                    || raw > std::numeric_limits<int>::max() || identity.isEmpty())
                    return fail("Xuanzhu observation identity is invalid");
                const int id = int(raw);
                if (conversion) {
                    const auto *ticket = responseTicket(request,id);
                    if (!ticket || ticket->activationRef.ownerObjectName != player->objectName()
                        || ticket->legacyIdentityToken != identity)
                        return fail("Xuanzhu observation does not match issued conversion identity");
                    lease = request.conversionLedger->observedProducts.value(id);
                    const Card *exact = lease ? lease() : nullptr;
                    // A display/policy clone is not the original producer object.
                    // Never equate them by costs, class, or toString alone.
                    if (!exact || aiLegacyCardIdentity(m_room,player,exact) != identity
                        || exact->toString() != ticket->legacyStringKey)
                        return fail("Xuanzhu conversion lacks matching exact producer custody");
                } else {
                    QList<AICardView> visible = request.worldView.handCards;
                    visible.append(request.worldView.discardPile);
                    visible.append(request.choiceOptions.cards);
                    QList<AIPlayerView> roster=request.worldView.players;roster.prepend(request.worldView.self);
                    for (const auto &p : roster) {
                        visible.append(p.equips);visible.append(p.judgingArea);visible.append(p.knownCards);
                        for (const auto &pile:p.piles)visible.append(pile.cards);
                    }
                    std::shared_ptr<const CardLifetimeToken> observed;
                    for (const auto &face:visible)
                        if (face.effectiveId==id && face.nativeIdentity && face.nativeIdentity->live
                            && aiLegacyCardIdentity(m_room,player,face.nativeIdentity)==identity) {
                            observed=face.nativeIdentity;break;
                        }
                    if (!observed) return fail("Xuanzhu physical identity is not currently disclosed");
                    // getEquips returns the real inner card, while hand views use
                    // the outer WrappedCard. Authenticate the exact projected one.
                    const Card *outer=Sanguosha->getCard(id), *exact=nullptr;
                    if (outer && globalCardLifetimeManager().liveToken(outer)==observed)exact=outer;
                    else if (outer && globalCardLifetimeManager().liveToken(outer->getRealCard())==observed)exact=outer->getRealCard();
                    if (!exact || exact->isVirtualCard()) return fail("Xuanzhu disclosed physical object changed");
                    lease=AIOwnerReadonlyCardStore::observe(exact);
                }
            } else return fail("Xuanzhu observation selector is not a finite issued identity");
            const QString token=writeAIRound53XuanzhuSlot(request,player,std::move(lease),names,proposal.value("source_fields").toObject(),&error);
            if(token.isEmpty())return fail(error);return{{"token",token}};
        }
        auto admit=[&](CardUseStruct&use){return m_skillRuntime.resolveCardSkillInstance(use);};
        if(mode=="first_select") {
            if(proposal.size()!=3||!proposal.value("recipe_key").isString())return fail("Xuanzhu first selected envelope malformed");
            const auto key=proposal.value("recipe_key").toString();
            if(m_room.roomRuntime()->ai().readonlyCardSource(parentRequest,player,key)!="xuanzhu_first_source")return fail("Xuanzhu selector is not its issued initial source");
            return issueAIRound53XuanzhuFirst(request,player,m_room.roomRuntime()->ai().retainReadonlyCard(parentRequest,player,key),admit);
        }
        if(mode=="read") {
            if(proposal.size()!=3||!proposal.value("include_card").isBool()||!aiRound53XuanzhuQuestion(request,player))return fail("Xuanzhu read lacks exact native question");
            auto saved=readAIRound53XuanzhuSlot(request,player,proposal.value("include_card").toBool());
            using R=AIRound53XuanzhuSlotRead;
            if(saved.kind==R::Unknown)return fail(saved.error);
            if(saved.kind==R::NilPlan)return{{"status","nil_plan"}};
            if(saved.kind==R::BooleanPlan)return{{"status","boolean_plan"}};
            if(saved.kind==R::TargetsError)return{{"status",saved.targetsNil?"nil_targets":"boolean_targets"}};
            if(saved.kind==R::CardError)return{{"status",saved.cardNil?"nil_card":"boolean_card"},{"targets",QJsonArray::fromStringList(saved.targets)}};
            QJsonObject out{{"status",saved.kind==R::Card?"card":"targets"},{"targets",QJsonArray::fromStringList(saved.targets)}};
            if(saved.kind==R::Card)out.insert("name",saved.name);return out;
        }
        if(mode=="saved_select") {
            if(proposal.size()!=5||!proposal.value("material").isDouble()||!proposal.value("name").isString()||!proposal.value("targets").isArray())return fail("Xuanzhu selected envelope malformed");
            double raw=proposal.value("material").toDouble(-1);if(raw<0||raw!=std::floor(raw)||raw>std::numeric_limits<int>::max())return fail("Xuanzhu selected material malformed");
            QStringList names;for(const auto&v:proposal.value("targets").toArray()){if(!v.isString()||v.toString().isEmpty())return fail("Xuanzhu selected target malformed");names<<v.toString();}
            return issueAIRound53XuanzhuSaved(request,player,int(raw),proposal.value("name").toString(),names,admit);
        }
        std::unique_ptr<Card>card;QString source;
        if(mode=="construct") {
            if(proposal.size()!=3||!proposal.value("name").isString())return fail("Xuanzhu constructor envelope malformed");
            card=constructAIRound53XuanzhuPolicy(request,player,proposal.value("name").toString(),&error);source="xuanzhu_source_dummy";
        } else if(mode=="first_construct") {
            if(proposal.size()!=2)return fail("Xuanzhu initial constructor accepts no authored fields");
            card=constructAIRound53XuanzhuFirst(request,player,&error);source="xuanzhu_first_source";
        } else return fail("Xuanzhu operation outside fixed family");
        if(!error.isEmpty())return fail(error);if(!card)return{{"exists",false}};
        const QString key=m_room.roomRuntime()->ai().issueReadonlyCard(parentRequest,player,std::move(card),source);
        const auto lease=m_room.roomRuntime()->ai().retainReadonlyCard(parentRequest,player,key);
        const Card*exact=lease?lease():nullptr;if(!exact)return fail("Xuanzhu readonly native custody missing");
        int budget=4096;const auto candidate=makeAICardCandidate(m_room,player,exact,exact->getHandlingMethod(),AIRequest::Activate,QString(),budget);
        QJsonObject targets=responseCandidateJson(candidate),row=publicDecisionCard(exact,true);
        row.insert("virtual_card",exact->isVirtualCard());row.insert("id",exact->getEffectiveId());row.insert("effective_id",exact->getEffectiveId());
        row.insert("subcards",responseIds(exact->getSubcards()));row.insert("legacy_string_key",exact->toString());
        row.insert("legacy_sort_key",legacySortKey(exact));row.insert("legacy_identity_token",aiLegacyCardIdentity(m_room,player,exact));
        row.insert("target_residue",responseResidueJson(responseResidue(m_room,player,exact)));
        row.insert("evaluation_rules",responseOwnerRules(m_room,player,exact));row.insert("locked",player->isLocked(exact));
        row.insert("using",exact->hasFlag("using"));row.insert("can_recast",exact->canRecast());
        row.insert("target_fixed",exact->targetFixed());row.insert("handling_method",int(exact->getHandlingMethod()));
        for(auto it=targets.begin();it!=targets.end();++it)row.insert(it.key(),it.value());
        row.insert("evaluation_only",true);row.insert("evaluation_owner",player->objectName());row.insert("readonly_recipe_key",key);
        if(!aiRound44RangeRequestCurrent(m_room,parentRequest)||lease()!=exact)return fail("Xuanzhu source query changed current state/custody");
        return{{"exists",true},{"evaluation_only",true},{"card",row},{"candidate",targets}};
    }
    if(proposal.value("kind")=="xufa_actual_slot") {
        auto fail=[](const QString &why){return QJsonObject{{"error",why},{"key","xufaUse"}};};
        auto *player=m_room.findPlayerByObjectName(request.viewerObjectName);
        if(!player || !aiRound44RangeRequestCurrent(m_room,parentRequest))return fail("Xufa active request is stale");
        EngineRuntimeContextScope engine(*Sanguosha,&m_room);
        LuaRuntime::Binding game(m_room.roomRuntime()->lua(),false);
        ResponseNativeScope native(m_room,player,request.reason,request.pattern);
        const QString mode=proposal.value("mode").toString();QString error;
        if(mode=="capture") {
            if(proposal.size()!=5||!proposal.value("selector").isObject()||!proposal.value("targets").isArray()||!proposal.value("source_fields").isObject())return fail("Xufa capture shape malformed");
            QStringList names;for(const auto &v:proposal.value("targets").toArray()){if(!v.isString()||v.toString().isEmpty())return fail("Xufa target malformed");names<<v.toString();}
            if(names.size()>128)return fail("Xufa target budget exceeded");
            const auto selector=proposal.value("selector").toObject();
            AIReadonlyCardLease lease;
            if (selector.size() == 1 && selector.value("readonly_recipe_key").isString()) {
                lease = m_room.roomRuntime()->ai().retainReadonlyCard(parentRequest,player,
                    selector.value("readonly_recipe_key").toString());
            } else if (selector.size() == 2 && selector.value("legacy_identity_token").isString()) {
                const QString identity = selector.value("legacy_identity_token").toString();
                const bool conversion = selector.contains("conversion_id");
                const auto value = selector.value(conversion ? "conversion_id" : "physical_id");
                const double raw = value.toDouble(-1);
                if (!value.isDouble() || raw < 0 || raw != std::floor(raw)
                    || raw > std::numeric_limits<int>::max() || identity.isEmpty())
                    return fail("Xufa observation identity is invalid");
                const int id = int(raw);
                if (conversion) {
                    const auto *ticket = responseTicket(request,id);
                    if (!ticket || ticket->activationRef.ownerObjectName != player->objectName()
                        || ticket->legacyIdentityToken != identity)
                        return fail("Xufa observation does not match issued conversion identity");
                    lease = request.conversionLedger->observedProducts.value(id);
                    const Card *exact = lease ? lease() : nullptr;
                    // A display/policy clone is not the original producer object.
                    // Never equate them by costs, class, or toString alone.
                    if (!exact || aiLegacyCardIdentity(m_room,player,exact) != identity
                        || exact->toString() != ticket->legacyStringKey)
                        return fail("Xufa conversion lacks matching exact producer custody");
                } else {
                    QList<AICardView> visible = request.worldView.handCards;
                    visible.append(request.worldView.discardPile);
                    visible.append(request.choiceOptions.cards);
                    QList<AIPlayerView> roster=request.worldView.players;roster.prepend(request.worldView.self);
                    for (const auto &p : roster) {
                        visible.append(p.equips);visible.append(p.judgingArea);visible.append(p.knownCards);
                        for (const auto &pile:p.piles)visible.append(pile.cards);
                    }
                    std::shared_ptr<const CardLifetimeToken> observed;
                    for (const auto &face:visible)
                        if (face.effectiveId==id && face.nativeIdentity && face.nativeIdentity->live
                            && aiLegacyCardIdentity(m_room,player,face.nativeIdentity)==identity) {
                            observed=face.nativeIdentity;break;
                        }
                    if (!observed) return fail("Xufa physical identity is not currently disclosed");
                    // getEquips returns the real inner card, while hand views use
                    // the outer WrappedCard. Authenticate the exact projected one.
                    const Card *outer=Sanguosha->getCard(id), *exact=nullptr;
                    if (outer && globalCardLifetimeManager().liveToken(outer)==observed)exact=outer;
                    else if (outer && globalCardLifetimeManager().liveToken(outer->getRealCard())==observed)exact=outer->getRealCard();
                    if (!exact || exact->isVirtualCard()) return fail("Xufa disclosed physical object changed");
                    lease=AIOwnerReadonlyCardStore::observe(exact);
                }
            } else return fail("Xufa observation selector is not a finite issued identity");
            const QString token=writeAIRound45XufaSlot(request,player,std::move(lease),names,proposal.value("source_fields").toObject(),&error);
            if(token.isEmpty())return fail(error);return{{"token",token}};
        }
        if(mode=="first_select"||mode=="saved_select") {
            const bool first=mode=="first_select";
            if(proposal.size()!=(first?3:4)||!proposal.value("recipe_key").isString()
                ||(!first&&!proposal.value("targets").isArray()))return fail("Xufa selected envelope malformed");
            const QString key=proposal.value("recipe_key").toString();
            const QString source=m_room.roomRuntime()->ai().readonlyCardSource(parentRequest,player,key);
            if(source!=(first?"xufa_first_source":"xufa_actual_slot"))return fail("Xufa selector is not its issued exact source");
            auto lease=m_room.roomRuntime()->ai().retainReadonlyCard(parentRequest,player,key);
            QStringList names;for(const auto&v:proposal.value("targets").toArray()){
                if(!v.isString()||v.toString().isEmpty())return fail("Xufa selected target malformed");names<<v.toString();}
            auto admit=[&](CardUseStruct&use){return m_skillRuntime.resolveCardSkillInstance(use);};
            return first?issueAIRound53XufaFirst(request,player,std::move(lease),admit)
                :issueAIRound53XufaSaved(request,player,std::move(lease),names,admit);
        }
        QString key;QStringList actualTargets;bool reading=false,targetsMissing=false,targetsFalse=false;
        if(mode=="first_construct") {
            if(proposal.size()!=3||!proposal.value("costs").isArray())return fail("Xufa first constructor shape malformed");
            QList<int>costs;for(const auto&v:proposal.value("costs").toArray()){
                double id=v.toDouble(-1);if(!v.isDouble()||id<0||id!=std::floor(id)||id>std::numeric_limits<int>::max())return fail("Xufa first cost malformed");costs<<int(id);}
            auto card=constructAIRound53XufaFirst(request,player,costs,&error);
            if(!card)return fail(error);
            key=m_room.roomRuntime()->ai().issueReadonlyCard(parentRequest,player,std::move(card),"xufa_first_source");
        } else if(mode=="read") {
            if(proposal.size()!=2)return fail("Xufa read accepts no authored plan");
            auto saved=readAIRound45XufaSlot(request,player);
            if(saved.kind==AIRound45XufaSlotRead::Unknown)return fail(saved.error);
            if(saved.kind==AIRound45XufaSlotRead::NilPlan)return{{"status","nil_plan"}};
            if(saved.kind==AIRound45XufaSlotRead::FalsePlan)return{{"status","false_plan"}};
            if(saved.kind==AIRound45XufaSlotRead::EmptyCard)return{{"status","empty_card"}};
            const Card*exact=saved.lease?saved.lease():nullptr;if(!exact)return fail("Xufa retained source expired");
            key=m_room.roomRuntime()->ai().issueBorrowedReadonlyCard(parentRequest,player,exact,"xufa_actual_slot");
            reading=true;actualTargets=saved.targets;targetsMissing=saved.targetsMissing;targetsFalse=saved.targetsFalse;
        } else if(mode=="construct") {
            if(proposal.size()!=4||!proposal.value("name").isString()||!proposal.value("costs").isArray())return fail("Xufa constructor shape malformed");
            QList<int>costs;for(const auto&v:proposal.value("costs").toArray()){const double id=v.toDouble(-1);if(!v.isDouble()||id<0||id!=std::floor(id)||id>std::numeric_limits<int>::max())return fail("Xufa cost malformed");costs<<int(id);}
            auto card=constructAIRound45XufaPolicy(request,player,proposal.value("name").toString(),costs,&error);
            if(!error.isEmpty())return fail(error);if(!card)return{{"exists",false}};
            key=m_room.roomRuntime()->ai().issueReadonlyCard(parentRequest,player,std::move(card),"xufa_source_dummy");
        } else return fail("Xufa source query mode invalid");
        const auto lease=m_room.roomRuntime()->ai().retainReadonlyCard(parentRequest,player,key);
        if(!lease||!lease())return fail("Xufa readonly source custody missing");
        const Card *exact=lease();QJsonObject row,targets;
        if(reading) {
            // The reader reaches canRecast first. No planning serializer,
            // sorting, targetFilter, targetFixed, flags or handling-method query.
            const bool recast=exact->canRecast();
            if(!aiRound44RangeRequestCurrent(m_room,parentRequest)||lease()!=exact)
                return fail("Xufa source canRecast changed current request or custody");
            row.insert("can_recast",recast);
            // The source returns (or errors indexing a missing target list)
            // before toString and before any selected binder in these branches.
            const bool sourceReturns=recast&&actualTargets.isEmpty()&&request.handlingMethod!=Card::MethodRecast;
            if(!targetsMissing&&!sourceReturns) {
                const QString wire=exact->toString();
                if(!aiRound44RangeRequestCurrent(m_room,parentRequest)||lease()!=exact)
                    return fail("Xufa source toString changed current request or custody");
                row.insert("legacy_string_key",wire);
                row.insert("legacy_identity_token",aiLegacyCardIdentity(m_room,player,exact));
                // Only selected answer's exact spec comparison; no class/kind,
                // availability, flag, sorting or hypothetical target metadata.
                row.insert("name",exact->objectName());row.insert("suit",int(exact->getSuit()));
                row.insert("number",exact->getNumber());row.insert("skill_name",exact->getSkillName(false));
                row.insert("subcards",responseIds(exact->getSubcards()));
            }
        } else {
            int budget=4096;
            const auto candidate=makeAICardCandidate(m_room,player,exact,mode=="first_construct"?exact->getHandlingMethod():Card::MethodUse,AIRequest::Activate,QString(),budget);
            targets=responseCandidateJson(candidate);row=publicDecisionCard(exact,true);
            row.insert("virtual_card",exact->isVirtualCard());
            row.insert("id",exact->getEffectiveId());row.insert("effective_id",exact->getEffectiveId());
            row.insert("subcards",responseIds(exact->getSubcards()));row.insert("legacy_string_key",exact->toString());
            row.insert("legacy_sort_key",legacySortKey(exact));row.insert("legacy_identity_token",aiLegacyCardIdentity(m_room,player,exact));
            row.insert("target_residue",responseResidueJson(responseResidue(m_room,player,exact)));
            row.insert("evaluation_rules",responseOwnerRules(m_room,player,exact));row.insert("locked",player->isLocked(exact));
            row.insert("using",exact->hasFlag("using"));row.insert("can_recast",exact->canRecast());
            row.insert("target_fixed",exact->targetFixed());row.insert("handling_method",int(exact->getHandlingMethod()));
            for(auto it=targets.begin();it!=targets.end();++it)row.insert(it.key(),it.value());
        }
        row.insert("evaluation_only",true);row.insert("evaluation_owner",player->objectName());row.insert("readonly_recipe_key",key);
        if(!aiRound44RangeRequestCurrent(m_room,parentRequest)||lease()!=exact)return fail("Xufa source query changed current state or custody");
        QJsonArray names;for(const auto&name:actualTargets)names.append(name);
        return{{"exists",true},{"evaluation_only",true},{"card",row},{"candidate",targets},{"status",reading?"card_plan":"constructed"},{"targets",names},{"targets_missing",targetsMissing},{"targets_false",targetsFalse}};
    }
    if(proposal.value("kind")=="olclan_saved_slot") {
        auto fail=[](const QString&why){return QJsonObject{{"error",why},{"key","olClan.saved_plan"}};};
        auto*player=m_room.findPlayerByObjectName(request.viewerObjectName);
        if(!player||!aiRound44RangeRequestCurrent(m_room,parentRequest))return fail("saved plan active request is stale");
        if(proposal.size()!=4||!proposal.value("slot").isString()||!proposal.value("operation").isString()||!proposal.value("payload").isObject())return fail("saved plan envelope malformed");
        EngineRuntimeContextScope engine(*Sanguosha,&m_room);LuaRuntime::Binding game(m_room.roomRuntime()->lua(),false);
        const auto slot=proposal.value("slot").toString(),op=proposal.value("operation").toString();
        const auto payload=proposal.value("payload").toObject();
        if(slot=="zulianzhuEp"||slot=="zujieliTo")return queryAIRound47OlClanPlayerSlot(parentRequest,player,slot,op,payload);
        if(slot=="zujiewusuit")return queryAIRound47OlClanScalarSlot(parentRequest,player,op,payload);
        if(slot=="lianzhanUse")return queryAIRound53LianzhanSlot(parentRequest,player,op,payload,[](const Card*c){return publicDecisionCard(c);});
        if(slot=="tenyearmingfa_c"&&op=="query")return queryAIRound53MingfaSlot(parentRequest,player,op,payload);
        if(slot=="diouUse"&&op=="select"){ResponseNativeScope native(m_room,player,parentRequest.reason,parentRequest.pattern);return issueAIRound53DiouReceipt(parentRequest,player,payload,[&](CardUseStruct&use){return m_skillRuntime.resolveCardSkillInstance(use);});}
        if(slot=="gengduUse"&&op=="write"&&payload.value("card_state")!="card")return queryAIRound53GengduSlot(parentRequest,player,op,payload);
        if(slot=="zhuning_d"&&op=="write"&&payload.value("card_state")!="card")return queryAIRound53ZhuningSlot(parentRequest,player,op,payload);
        if(op=="write"){
            const auto selector=payload.value("selector").toObject();
            AIReadonlyCardLease lease;
            if (selector.size() == 3 && selector.value("acquired_source_id").isDouble()
                && selector.value("legacy_identity_token").isString()
                && selector.value("observed_source_identity").isString()) {
                // Native request-owned profile/source association, never a Lua
                // claim that a temporary evaluation clone is the offered Card.
                const QString reason = slot == "kezumingjieUse" ? "kezumingjie"
                    : slot == "kezulilunUse" ? "kezulilun" : QString();
                const double raw = selector.value("acquired_source_id").toDouble(-1);
                if (reason.isEmpty() || request.kind != AIRequest::AmazingGrace
                    || request.choiceOptions.reason != reason || raw < 0 || raw != std::floor(raw)
                    || raw > std::numeric_limits<int>::max()) return fail("saved acquired source inquiry mismatch");
                const int id = int(raw);
                const auto acquired = request.choiceOptions.context.value("acquired_card_evaluations").toObject().value(QString::number(id)).toObject();
                const auto source = acquired.value("observed_source").toObject();
                const QString sourceIdentity = selector.value("observed_source_identity").toString();
                const QString profileIdentity = selector.value("legacy_identity_token").toString();
                if (!request.choiceOptions.cardIds.contains(id) || sourceIdentity.isEmpty() || profileIdentity.isEmpty()
                    || source.value("physical_id") != QJsonValue(id)
                    || source.value("legacy_identity_token").toString() != sourceIdentity
                    || acquired.value("card").toObject().value("legacy_identity_token").toString() != profileIdentity)
                    return fail("saved acquired source differs from native profile witness");
                const Card *actual = Sanguosha->getCard(id);
                bool observed = false;
                for (const auto &face : request.choiceOptions.cards)
                    if (face.effectiveId == id && face.nativeIdentity
                        && globalCardLifetimeManager().isLive(face.nativeIdentity)
                        && face.nativeIdentity->address == actual
                        && aiLegacyCardIdentity(m_room,player,face.nativeIdentity) == sourceIdentity) observed = true;
                if (!observed || !actual || actual->isVirtualCard()) return fail("saved acquired source is no longer disclosed");
                // queryAIRound47OlClanSlot rechecks the full current face and
                // exact source inquiry before assignment; no payment authority.
                lease = AIOwnerReadonlyCardStore::observe(actual);
            } else if (selector.size() == 1 && selector.value("readonly_recipe_key").isString()) {
                lease = m_room.roomRuntime()->ai().retainReadonlyCard(parentRequest,player,
                    selector.value("readonly_recipe_key").toString());
            } else if (selector.size() == 2 && selector.value("legacy_identity_token").isString()) {
                const QString identity = selector.value("legacy_identity_token").toString();
                const bool conversion = selector.contains("conversion_id");
                const auto value = selector.value(conversion ? "conversion_id" : "physical_id");
                const double raw = value.toDouble(-1);
                if (!value.isDouble() || raw < 0 || raw != std::floor(raw)
                    || raw > std::numeric_limits<int>::max() || identity.isEmpty())
                    return fail("saved plan observation identity is invalid");
                const int id = int(raw);
                if (conversion) {
                    const auto *ticket = responseTicket(request,id);
                    if (!ticket || ticket->activationRef.ownerObjectName != player->objectName()
                        || ticket->legacyIdentityToken != identity)
                        return fail("saved plan observation does not match issued conversion identity");
                    lease = request.conversionLedger->observedProducts.value(id);
                    const Card *exact = lease ? lease() : nullptr;
                    // A display/policy clone is not the original producer object.
                    // Never equate them by costs, class, or toString alone.
                    if (!exact || aiLegacyCardIdentity(m_room,player,exact) != identity
                        || exact->toString() != ticket->legacyStringKey)
                        return fail("saved plan conversion lacks matching exact producer custody");
                } else {
                    QList<AICardView> visible = request.worldView.handCards;
                    visible.append(request.worldView.discardPile);
                    visible.append(request.choiceOptions.cards);
                    QList<AIPlayerView> roster=request.worldView.players;roster.prepend(request.worldView.self);
                    for (const auto &p : roster) {
                        visible.append(p.equips);visible.append(p.judgingArea);visible.append(p.knownCards);
                        for (const auto &pile:p.piles)visible.append(pile.cards);
                    }
                    std::shared_ptr<const CardLifetimeToken> observed;
                    for (const auto &face:visible)
                        if (face.effectiveId==id && face.nativeIdentity && face.nativeIdentity->live
                            && aiLegacyCardIdentity(m_room,player,face.nativeIdentity)==identity) {
                            observed=face.nativeIdentity;break;
                        }
                    if (!observed) return fail("saved plan physical identity is not currently disclosed");
                    // getEquips returns the real inner card, while hand views use
                    // the outer WrappedCard. Authenticate the exact projected one.
                    const Card *outer=Sanguosha->getCard(id), *exact=nullptr;
                    if (outer && globalCardLifetimeManager().liveToken(outer)==observed)exact=outer;
                    else if (outer && globalCardLifetimeManager().liveToken(outer->getRealCard())==observed)exact=outer->getRealCard();
                    if (!exact || exact->isVirtualCard()) return fail("saved plan disclosed physical object changed");
                    lease=AIOwnerReadonlyCardStore::observe(exact);
                }
            } else return fail("saved plan observation selector is not a finite issued identity");
            if(slot=="tyfenwuUse")return queryAIRound53TyfenwuSlot(parentRequest,player,op,payload,std::move(lease));
            if(slot=="tychendeUse")return queryAIRound53TychendeSlot(parentRequest,player,op,payload,std::move(lease));
            if(slot=="tyqingkouUse")return queryAIRound53TyqingkouSlot(parentRequest,player,op,payload,std::move(lease));
            if(slot=="diouUse")return queryAIRound53DiouSlot(parentRequest,player,op,payload,std::move(lease));
            if(slot=="zhuning_d")return queryAIRound53ZhuningSlot(parentRequest,player,op,payload,std::move(lease));
            if(slot=="gengduUse")return queryAIRound53GengduSlot(parentRequest,player,op,payload,std::move(lease));
            if(slot=="tenyearmingfa_c")return queryAIRound53MingfaSlot(parentRequest,player,op,payload,std::move(lease));
            return queryAIRound47OlClanSlot(parentRequest,player,slot,op,payload,std::move(lease));
        }
        if(slot=="tyfenwuUse")return queryAIRound53TyfenwuSlot(parentRequest,player,op,payload);
        if(slot=="tychendeUse")return queryAIRound53TychendeSlot(parentRequest,player,op,payload);
        if(slot=="tyqingkouUse")return queryAIRound53TyqingkouSlot(parentRequest,player,op,payload);
        if(slot=="diouUse")return queryAIRound53DiouSlot(parentRequest,player,op,payload);
        if(slot=="zhuning_d")return queryAIRound53ZhuningSlot(parentRequest,player,op,payload);
        if(slot=="gengduUse")return queryAIRound53GengduSlot(parentRequest,player,op,payload);
        return queryAIRound47OlClanSlot(parentRequest,player,slot,op,payload);
    }
    if(proposal.value("kind")=="newgenerals_saved_slot") {
        auto fail=[](const QString&why){return QJsonObject{{"error",why},{"key","newgenerals.saved_plan"}};};
        auto*player=m_room.findPlayerByObjectName(request.viewerObjectName);
        if(!player||!aiRound44RangeRequestCurrent(m_room,parentRequest))return fail("saved plan active request is stale");
        if(proposal.size()!=4||!proposal.value("slot").isString()||!proposal.value("operation").isString()||!proposal.value("payload").isObject())return fail("saved plan envelope malformed");
        EngineRuntimeContextScope engine(*Sanguosha,&m_room);LuaRuntime::Binding game(m_room.roomRuntime()->lua(),false);
        const auto slot=proposal.value("slot").toString(),op=proposal.value("operation").toString();
        const auto payload=proposal.value("payload").toObject();
        if(op=="write"&&slot!="xdyifuvsTo"){
            const auto selector=payload.value("selector").toObject();
            AIReadonlyCardLease lease;
            if (selector.size() == 1 && selector.value("readonly_recipe_key").isString()) {
                lease = m_room.roomRuntime()->ai().retainReadonlyCard(parentRequest,player,
                    selector.value("readonly_recipe_key").toString());
            } else if (selector.size() == 2 && selector.value("legacy_identity_token").isString()) {
                const QString identity = selector.value("legacy_identity_token").toString();
                const bool conversion = selector.contains("conversion_id");
                const auto value = selector.value(conversion ? "conversion_id" : "physical_id");
                const double raw = value.toDouble(-1);
                if (!value.isDouble() || raw < 0 || raw != std::floor(raw)
                    || raw > std::numeric_limits<int>::max() || identity.isEmpty())
                    return fail("saved plan observation identity is invalid");
                const int id = int(raw);
                if (conversion) {
                    const auto *ticket = responseTicket(request,id);
                    if (!ticket || ticket->activationRef.ownerObjectName != player->objectName()
                        || ticket->legacyIdentityToken != identity)
                        return fail("saved plan observation does not match issued conversion identity");
                    lease = request.conversionLedger->observedProducts.value(id);
                    const Card *exact = lease ? lease() : nullptr;
                    // A display/policy clone is not the original producer object.
                    // Never equate them by costs, class, or toString alone.
                    if (!exact || aiLegacyCardIdentity(m_room,player,exact) != identity
                        || exact->toString() != ticket->legacyStringKey)
                        return fail("saved plan conversion lacks matching exact producer custody");
                } else {
                    QList<AICardView> visible = request.worldView.handCards;
                    visible.append(request.worldView.discardPile);
                    visible.append(request.choiceOptions.cards);
                    QList<AIPlayerView> roster=request.worldView.players;roster.prepend(request.worldView.self);
                    for (const auto &p : roster) {
                        visible.append(p.equips);visible.append(p.judgingArea);visible.append(p.knownCards);
                        for (const auto &pile:p.piles)visible.append(pile.cards);
                    }
                    std::shared_ptr<const CardLifetimeToken> observed;
                    for (const auto &face:visible)
                        if (face.effectiveId==id && face.nativeIdentity && face.nativeIdentity->live
                            && aiLegacyCardIdentity(m_room,player,face.nativeIdentity)==identity) {
                            observed=face.nativeIdentity;break;
                        }
                    if (!observed) return fail("saved plan physical identity is not currently disclosed");
                    // getEquips returns the real inner card, while hand views use
                    // the outer WrappedCard. Authenticate the exact projected one.
                    const Card *outer=Sanguosha->getCard(id), *exact=nullptr;
                    if (outer && globalCardLifetimeManager().liveToken(outer)==observed)exact=outer;
                    else if (outer && globalCardLifetimeManager().liveToken(outer->getRealCard())==observed)exact=outer->getRealCard();
                    if (!exact || exact->isVirtualCard()) return fail("saved plan disclosed physical object changed");
                    lease=AIOwnerReadonlyCardStore::observe(exact);
                }
            } else return fail("saved plan observation selector is not a finite issued identity");
            return queryAIRound46PlanningSlot(parentRequest,player,slot,op,payload,std::move(lease));
        }
        return queryAIRound46PlanningSlot(parentRequest,player,slot,op,payload);
    }
    if(proposal.value("kind")=="heqia_saved_plan") {
        auto fail=[](const QString &why){return QJsonObject{{"error",why},{"key","heqia_use"}};};
        auto *player=m_room.findPlayerByObjectName(request.viewerObjectName);
        if(!player || !aiRound44RangeRequestCurrent(m_room,parentRequest))return fail("Heqia active request is stale");
        EngineRuntimeContextScope engine(*Sanguosha,&m_room);
        LuaRuntime::Binding game(m_room.roomRuntime()->lua(),false);
        ResponseNativeScope native(m_room,player,request.reason,request.pattern);
        const QString mode=proposal.value("mode").toString();
        QString error;
        if(mode=="capture") {
            if((proposal.size()!=5 && proposal.size()!=6) || (proposal.size()==6&&!proposal.value("source_fields").isObject())
                || !proposal.value("selector").isObject() || !proposal.value("targets").isArray()
                || !proposal.value("offered_id").isDouble())return fail("Heqia capture shape malformed");
            const int offered=proposal.value("offered_id").toInt(-1);if(offered<0||proposal.value("offered_id").toDouble()!=offered)return fail("Heqia offer is not an integer");
            QStringList names;for(const auto &value:proposal.value("targets").toArray()){
                if(!value.isString()||value.toString().isEmpty())return fail("Heqia target malformed");names<<value.toString();}
            if(names.size()>128)return fail("Heqia target budget exceeded");
            const auto selector=proposal.value("selector").toObject();
            AIReadonlyCardLease lease;
            if(selector.isEmpty()&&proposal.value("source_fields").toObject().value("card_state")!="card") {}
            else if (selector.size() == 1 && selector.value("readonly_recipe_key").isString()) {
                lease = m_room.roomRuntime()->ai().retainReadonlyCard(parentRequest,player,
                    selector.value("readonly_recipe_key").toString());
            } else if (selector.size() == 2 && selector.value("legacy_identity_token").isString()) {
                const QString identity = selector.value("legacy_identity_token").toString();
                const bool conversion = selector.contains("conversion_id");
                const auto value = selector.value(conversion ? "conversion_id" : "physical_id");
                const double raw = value.toDouble(-1);
                if (!value.isDouble() || raw < 0 || raw != std::floor(raw)
                    || raw > std::numeric_limits<int>::max() || identity.isEmpty())
                    return fail("Heqia observation identity is invalid");
                const int id = int(raw);
                if (conversion) {
                    const auto *ticket = responseTicket(request,id);
                    if (!ticket || ticket->activationRef.ownerObjectName != player->objectName()
                        || ticket->legacyIdentityToken != identity)
                        return fail("Heqia observation does not match issued conversion identity");
                    lease = request.conversionLedger->observedProducts.value(id);
                    const Card *exact = lease ? lease() : nullptr;
                    // A display/policy clone is not the original producer object.
                    // Never equate them by costs, class, or toString alone.
                    if (!exact || aiLegacyCardIdentity(m_room,player,exact) != identity
                        || exact->toString() != ticket->legacyStringKey)
                        return fail("Heqia conversion lacks matching exact producer custody");
                } else {
                    QList<AICardView> visible = request.worldView.handCards;
                    visible.append(request.worldView.discardPile);
                    visible.append(request.choiceOptions.cards);
                    QList<AIPlayerView> roster=request.worldView.players;roster.prepend(request.worldView.self);
                    for (const auto &p : roster) {
                        visible.append(p.equips);visible.append(p.judgingArea);visible.append(p.knownCards);
                        for (const auto &pile:p.piles)visible.append(pile.cards);
                    }
                    std::shared_ptr<const CardLifetimeToken> observed;
                    for (const auto &face:visible)
                        if (face.effectiveId==id && face.nativeIdentity && face.nativeIdentity->live
                            && aiLegacyCardIdentity(m_room,player,face.nativeIdentity)==identity) {
                            observed=face.nativeIdentity;break;
                        }
                    if (!observed) return fail("Heqia physical identity is not currently disclosed");
                    // getEquips returns the real inner card, while hand views use
                    // the outer WrappedCard. Authenticate the exact projected one.
                    const Card *outer=Sanguosha->getCard(id), *exact=nullptr;
                    if (outer && globalCardLifetimeManager().liveToken(outer)==observed)exact=outer;
                    else if (outer && globalCardLifetimeManager().liveToken(outer->getRealCard())==observed)exact=outer->getRealCard();
                    if (!exact || exact->isVirtualCard()) return fail("Heqia disclosed physical object changed");
                    lease=AIOwnerReadonlyCardStore::observe(exact);
                }
            } else return fail("Heqia observation selector is not a finite issued identity");
            const QString token=writeAIRound47HeqiaSlot(request,player,offered,std::move(lease),names,proposal.value("source_fields").toObject(),&error);
            if(token.isEmpty())return fail(error);return{{"token",token}};
        }
        if(mode=="select"||mode=="first") {
            if(proposal.size()!=4||!proposal.value("targets").isArray())return fail("Heqia selected shape malformed");
            QStringList names;for(const auto&v:proposal.value("targets").toArray()){if(!v.isString())return fail("Heqia selected target malformed");names<<v.toString();}
            if(names.size()>128)return fail("Heqia target budget exceeded");
            if(mode=="select"){
                if(!proposal.value("token").isString())return fail("Heqia selected custody key malformed");
                return issueAIRound47HeqiaReceipt(request,player,proposal.value("token").toString(),names,[&](CardUseStruct&use){return m_skillRuntime.resolveCardSkillInstance(use);});
            }
            if(!proposal.value("subcards").isArray())return fail("Heqia first payment malformed");QList<int>ids;
            for(const auto&v:proposal.value("subcards").toArray()){const int id=v.toInt(-1);if(!v.isDouble()||id<0||v.toDouble()!=id)return fail("Heqia first payment ID malformed");ids<<id;}
            return issueAIRound47HeqiaFirst(request,player,ids,names,[&](CardUseStruct&use){return m_skillRuntime.resolveCardSkillInstance(use);});
        }
        QString key;QStringList observedTargets;
        if(mode=="read"){
            if(proposal.size()!=3||!proposal.value("token").isString())return fail("Heqia read shape malformed");
            AIRound47HeqiaSlotStatus status;const Card*c=readAIRound47HeqiaSlot(request,player,proposal.value("token").toString(),&observedTargets,nullptr,&error,&status);
            if(status==AIRound47HeqiaSlotStatus::SourcePass)return{{"source_pass",true}};
            if(status==AIRound47HeqiaSlotStatus::SourceError)return{{"source_error",error}};
            if(!c)return fail(error);
            key=m_room.roomRuntime()->ai().issueBorrowedReadonlyCard(parentRequest,player,c,"heqia_actual_slot");
        }else{
            if(mode!="construct"||proposal.size()!=5||!proposal.value("name").isString()||!proposal.value("offered_id").isDouble()||!proposal.value("material").isDouble())return fail("Heqia constructor shape malformed");
            const int offer=proposal.value("offered_id").toInt(-1),material=proposal.value("material").toInt(-1);
            if(offer<0||material<0||offer!=proposal.value("offered_id").toDouble()||material!=proposal.value("material").toDouble())return fail("Heqia constructor ID malformed");
            bool exactName=false;for(const auto&v:request.choiceOptions.cards)if(v.effectiveId==offer&&v.objectName==proposal.value("name").toString())exactName=true;
            if(!exactName)return fail("Heqia source offered name changed");
            auto card=constructAIRound47Heqia(request,player,offer,material,&error);
            if(!error.isEmpty())return fail(error);if(!card)return{{"exists",false}};
            key=m_room.roomRuntime()->ai().issueReadonlyCard(parentRequest,player,std::move(card),"heqia_ag");
        }
        const auto lease=m_room.roomRuntime()->ai().retainReadonlyCard(parentRequest,player,key);
        if(!lease||!lease())return fail("Heqia readonly constructor custody missing");
        const Card *exact=lease();int budget=4096;
        const auto candidate=makeAICardCandidate(m_room,player,exact,Card::MethodUse,AIRequest::Activate,QString(),budget);
        auto row=publicDecisionCard(exact,true);const auto targets=responseCandidateJson(candidate);
        row.insert("evaluation_only",true);row.insert("evaluation_owner",player->objectName());
        row.insert("readonly_recipe_key",key);row.insert("virtual_card",exact->isVirtualCard());
        row.insert("id",exact->getEffectiveId());row.insert("effective_id",exact->getEffectiveId());
        row.insert("subcards",responseIds(exact->getSubcards()));row.insert("legacy_string_key",exact->toString());
        row.insert("legacy_sort_key",legacySortKey(exact));row.insert("legacy_identity_token",aiLegacyCardIdentity(m_room,player,exact));
        row.insert("target_residue",responseResidueJson(responseResidue(m_room,player,exact)));
        row.insert("evaluation_rules",responseOwnerRules(m_room,player,exact));row.insert("locked",player->isLocked(exact));
        row.insert("using",exact->hasFlag("using"));row.insert("can_recast",exact->canRecast());
        row.insert("target_fixed",exact->targetFixed());row.insert("handling_method",int(exact->getHandlingMethod()));
        for(auto it=targets.begin();it!=targets.end();++it)row.insert(it.key(),it.value());
        if(!aiRound44RangeRequestCurrent(m_room,parentRequest))return fail("Heqia source constructor query changed state");
        return{{"exists",true},{"evaluation_only",true},{"card",row},{"candidate",targets},{"targets",QJsonArray::fromStringList(observedTargets)}};
    }
    if(proposal.value("kind")=="leiluan_saved_plan") {
        auto fail=[](const QString &why){return QJsonObject{{"error",why},{"key","leiluanUse"}};};
        auto *player=m_room.findPlayerByObjectName(request.viewerObjectName);
        if(!player || !aiRound44RangeRequestCurrent(m_room,parentRequest))return fail("Leiluan active request is stale");
        EngineRuntimeContextScope engine(*Sanguosha,&m_room);
        LuaRuntime::Binding game(m_room.roomRuntime()->lua(),false);
        ResponseNativeScope native(m_room,player,request.reason,request.pattern);
        const QString mode=proposal.value("mode").toString();
        QString error;
        if(mode=="capture") {
            if((proposal.size()!=5 && proposal.size()!=6) || (proposal.size()==6&&!proposal.value("source_fields").isObject())
                || !proposal.value("selector").isObject() || !proposal.value("targets").isArray()
                || !proposal.value("offered_name").isString())return fail("Leiluan capture shape malformed");
            const QString offered=proposal.value("offered_name").toString();
            QStringList names;for(const auto &value:proposal.value("targets").toArray()){
                if(!value.isString()||value.toString().isEmpty())return fail("Leiluan target malformed");names<<value.toString();}
            if(names.size()>128)return fail("Leiluan target budget exceeded");
            const auto selector=proposal.value("selector").toObject();
            AIReadonlyCardLease lease;
            if (selector.size() == 1 && selector.value("readonly_recipe_key").isString()) {
                lease = m_room.roomRuntime()->ai().retainReadonlyCard(parentRequest,player,
                    selector.value("readonly_recipe_key").toString());
            } else if (selector.size() == 2 && selector.value("legacy_identity_token").isString()) {
                const QString identity = selector.value("legacy_identity_token").toString();
                const bool conversion = selector.contains("conversion_id");
                const auto value = selector.value(conversion ? "conversion_id" : "physical_id");
                const double raw = value.toDouble(-1);
                if (!value.isDouble() || raw < 0 || raw != std::floor(raw)
                    || raw > std::numeric_limits<int>::max() || identity.isEmpty())
                    return fail("Leiluan observation identity is invalid");
                const int id = int(raw);
                if (conversion) {
                    const auto *ticket = responseTicket(request,id);
                    if (!ticket || ticket->activationRef.ownerObjectName != player->objectName()
                        || ticket->legacyIdentityToken != identity)
                        return fail("Leiluan observation does not match issued conversion identity");
                    lease = request.conversionLedger->observedProducts.value(id);
                    const Card *exact = lease ? lease() : nullptr;
                    // A display/policy clone is not the original producer object.
                    // Never equate them by costs, class, or toString alone.
                    if (!exact || aiLegacyCardIdentity(m_room,player,exact) != identity
                        || exact->toString() != ticket->legacyStringKey)
                        return fail("Leiluan conversion lacks matching exact producer custody");
                } else {
                    QList<AICardView> visible = request.worldView.handCards;
                    visible.append(request.worldView.discardPile);
                    visible.append(request.choiceOptions.cards);
                    QList<AIPlayerView> roster=request.worldView.players;roster.prepend(request.worldView.self);
                    for (const auto &p : roster) {
                        visible.append(p.equips);visible.append(p.judgingArea);visible.append(p.knownCards);
                        for (const auto &pile:p.piles)visible.append(pile.cards);
                    }
                    std::shared_ptr<const CardLifetimeToken> observed;
                    for (const auto &face:visible)
                        if (face.effectiveId==id && face.nativeIdentity && face.nativeIdentity->live
                            && aiLegacyCardIdentity(m_room,player,face.nativeIdentity)==identity) {
                            observed=face.nativeIdentity;break;
                        }
                    if (!observed) return fail("Leiluan physical identity is not currently disclosed");
                    // getEquips returns the real inner card, while hand views use
                    // the outer WrappedCard. Authenticate the exact projected one.
                    const Card *outer=Sanguosha->getCard(id), *exact=nullptr;
                    if (outer && globalCardLifetimeManager().liveToken(outer)==observed)exact=outer;
                    else if (outer && globalCardLifetimeManager().liveToken(outer->getRealCard())==observed)exact=outer->getRealCard();
                    if (!exact || exact->isVirtualCard()) return fail("Leiluan disclosed physical object changed");
                    lease=AIOwnerReadonlyCardStore::observe(exact);
                }
            } else return fail("Leiluan observation selector is not a finite issued identity");
            const QString token=writeAIRound47BLeiluanSlot(request,player,offered,std::move(lease),names,proposal.value("source_fields").toObject(),&error);
            if(token.isEmpty())return fail(error);return{{"token",token}};
        }
        if(mode=="select") {
            if(proposal.size()!=3||!proposal.value("token").isString())return fail("Leiluan select shape malformed");
            return issueAIRound47BLeiluanReceipt(request,player,proposal.value("token").toString(),[&](CardUseStruct &use){
                return m_skillRuntime.resolveCardSkillInstance(use);
            });
        }
        if(mode!="construct" || proposal.size()!=3 || !proposal.value("name").isString())return fail("Leiluan constructor shape malformed");
        auto card=constructAIRound47BLeiluan(request,player,proposal.value("name").toString(),&error);
        if(!error.isEmpty())return fail(error);if(!card)return{{"exists",false}};
        const auto key=m_room.roomRuntime()->ai().issueReadonlyCard(parentRequest,player,std::move(card),"leiluan_choice");
        const auto lease=m_room.roomRuntime()->ai().retainReadonlyCard(parentRequest,player,key);
        if(!lease||!lease())return fail("Leiluan readonly constructor custody missing");
        const Card *exact=lease();int budget=4096;
        const auto candidate=makeAICardCandidate(m_room,player,exact,Card::MethodUse,AIRequest::Activate,QString(),budget);
        auto row=publicDecisionCard(exact,true);const auto targets=responseCandidateJson(candidate);
        row.insert("evaluation_only",true);row.insert("evaluation_owner",player->objectName());
        row.insert("readonly_recipe_key",key);row.insert("virtual_card",exact->isVirtualCard());
        row.insert("id",exact->getEffectiveId());row.insert("effective_id",exact->getEffectiveId());
        row.insert("subcards",responseIds(exact->getSubcards()));row.insert("legacy_string_key",exact->toString());
        row.insert("legacy_sort_key",legacySortKey(exact));row.insert("legacy_identity_token",aiLegacyCardIdentity(m_room,player,exact));
        row.insert("target_residue",responseResidueJson(responseResidue(m_room,player,exact)));
        row.insert("evaluation_rules",responseOwnerRules(m_room,player,exact));row.insert("locked",player->isLocked(exact));
        row.insert("using",exact->hasFlag("using"));row.insert("can_recast",exact->canRecast());
        row.insert("target_fixed",exact->targetFixed());row.insert("handling_method",int(exact->getHandlingMethod()));
        for(auto it=targets.begin();it!=targets.end();++it)row.insert(it.key(),it.value());
        if(!aiRound44RangeRequestCurrent(m_room,parentRequest))return fail("Leiluan source constructor query changed state");
        return{{"exists",true},{"evaluation_only",true},{"card",row},{"candidate",targets}};
    }
    if(proposal.value("class_name")=="@r51_sushou_raw_costs") {
        const auto raw=proposal.value("spec").toObject();
        if(proposal.size()!=3 || proposal.value("scope_id")!=0 || raw.size()!=1 || !raw.value("wire").isString())return QJsonObject{{"error","Sushou raw source proposal malformed"}};
        auto *player=m_room.findPlayerByObjectName(request.viewerObjectName);
        if(!player || !aiRound44RangeRequestCurrent(m_room,parentRequest))return QJsonObject{{"error","Sushou raw source request stale"}};
        EngineRuntimeContextScope engine(*Sanguosha,&m_room);
        LuaRuntime::Binding game(m_room.roomRuntime()->lua(),false);
        ResponseNativeScope native(m_room,player,request.reason,request.pattern);
        return issueAIRound51NotifiedCosts(request,player,raw.value("wire").toString(),
            [&](CardUseStruct &use){return m_skillRuntime.resolveCardSkillInstance(use);});
    }
    if(proposal.value("kind")=="jianman_saved_plan") {
        auto fail=[](const QString &why){return QJsonObject{{"error",why},{"key","jianmanUse"}};};
        auto *player=m_room.findPlayerByObjectName(request.viewerObjectName);
        if(!player || !aiRound44RangeRequestCurrent(m_room,parentRequest))return fail("Jianman active request is stale");
        EngineRuntimeContextScope engine(*Sanguosha,&m_room);
        LuaRuntime::Binding game(m_room.roomRuntime()->lua(),false);
        ResponseNativeScope native(m_room,player,request.reason,request.pattern);
        const QString mode=proposal.value("mode").toString();
        QString error;
        if(mode=="capture") {
            if((proposal.size()!=5 && proposal.size()!=6) || (proposal.size()==6&&!proposal.value("source_fields").isObject())
                || !proposal.value("selector").isObject() || !proposal.value("targets").isArray()
                || !proposal.value("offered_name").isString())return fail("Jianman capture shape malformed");
            const QString offered=proposal.value("offered_name").toString();
            QStringList names;for(const auto &value:proposal.value("targets").toArray()){
                if(!value.isString()||value.toString().isEmpty())return fail("Jianman target malformed");names<<value.toString();}
            if(names.size()>128)return fail("Jianman target budget exceeded");
            const auto selector=proposal.value("selector").toObject();
            AIReadonlyCardLease lease;
            if (selector.size() == 1 && selector.value("readonly_recipe_key").isString()) {
                lease = m_room.roomRuntime()->ai().retainReadonlyCard(parentRequest,player,
                    selector.value("readonly_recipe_key").toString());
            } else if (selector.size() == 2 && selector.value("legacy_identity_token").isString()) {
                const QString identity = selector.value("legacy_identity_token").toString();
                const bool conversion = selector.contains("conversion_id");
                const auto value = selector.value(conversion ? "conversion_id" : "physical_id");
                const double raw = value.toDouble(-1);
                if (!value.isDouble() || raw < 0 || raw != std::floor(raw)
                    || raw > std::numeric_limits<int>::max() || identity.isEmpty())
                    return fail("Jianman observation identity is invalid");
                const int id = int(raw);
                if (conversion) {
                    const auto *ticket = responseTicket(request,id);
                    if (!ticket || ticket->activationRef.ownerObjectName != player->objectName()
                        || ticket->legacyIdentityToken != identity)
                        return fail("Jianman observation does not match issued conversion identity");
                    lease = request.conversionLedger->observedProducts.value(id);
                    const Card *exact = lease ? lease() : nullptr;
                    // A display/policy clone is not the original producer object.
                    // Never equate them by costs, class, or toString alone.
                    if (!exact || aiLegacyCardIdentity(m_room,player,exact) != identity
                        || exact->toString() != ticket->legacyStringKey)
                        return fail("Jianman conversion lacks matching exact producer custody");
                } else {
                    QList<AICardView> visible = request.worldView.handCards;
                    visible.append(request.worldView.discardPile);
                    visible.append(request.choiceOptions.cards);
                    QList<AIPlayerView> roster=request.worldView.players;roster.prepend(request.worldView.self);
                    for (const auto &p : roster) {
                        visible.append(p.equips);visible.append(p.judgingArea);visible.append(p.knownCards);
                        for (const auto &pile:p.piles)visible.append(pile.cards);
                    }
                    std::shared_ptr<const CardLifetimeToken> observed;
                    for (const auto &face:visible)
                        if (face.effectiveId==id && face.nativeIdentity && face.nativeIdentity->live
                            && aiLegacyCardIdentity(m_room,player,face.nativeIdentity)==identity) {
                            observed=face.nativeIdentity;break;
                        }
                    if (!observed) return fail("Jianman physical identity is not currently disclosed");
                    // getEquips returns the real inner card, while hand views use
                    // the outer WrappedCard. Authenticate the exact projected one.
                    const Card *outer=Sanguosha->getCard(id), *exact=nullptr;
                    if (outer && globalCardLifetimeManager().liveToken(outer)==observed)exact=outer;
                    else if (outer && globalCardLifetimeManager().liveToken(outer->getRealCard())==observed)exact=outer->getRealCard();
                    if (!exact || exact->isVirtualCard()) return fail("Jianman disclosed physical object changed");
                    lease=AIOwnerReadonlyCardStore::observe(exact);
                }
            } else return fail("Jianman observation selector is not a finite issued identity");
            const QString token=writeAIRound46JianmanSlot(request,player,offered,std::move(lease),names,proposal.value("source_fields").toObject(),&error);
            if(token.isEmpty())return fail(error);return{{"token",token}};
        }
        if(mode=="select") {
            if(proposal.size()!=3||!proposal.value("token").isString())return fail("Jianman select shape malformed");
            return issueAIRound46JianmanReceipt(request,player,proposal.value("token").toString(),[&](CardUseStruct &use){
                return m_skillRuntime.resolveCardSkillInstance(use);
            });
        }
        if(mode!="construct" || proposal.size()!=3 || !proposal.value("name").isString())return fail("Jianman constructor shape malformed");
        auto card=constructAIRound46Jianman(request,player,proposal.value("name").toString(),&error);
        if(!error.isEmpty())return fail(error);if(!card)return{{"exists",false}};
        const auto key=m_room.roomRuntime()->ai().issueReadonlyCard(parentRequest,player,std::move(card),"jianman_choice");
        const auto lease=m_room.roomRuntime()->ai().retainReadonlyCard(parentRequest,player,key);
        if(!lease||!lease())return fail("Jianman readonly constructor custody missing");
        const Card *exact=lease();int budget=4096;
        const auto candidate=makeAICardCandidate(m_room,player,exact,Card::MethodUse,AIRequest::Activate,QString(),budget);
        auto row=publicDecisionCard(exact,true);const auto targets=responseCandidateJson(candidate);
        row.insert("evaluation_only",true);row.insert("evaluation_owner",player->objectName());
        row.insert("readonly_recipe_key",key);row.insert("virtual_card",exact->isVirtualCard());
        row.insert("id",exact->getEffectiveId());row.insert("effective_id",exact->getEffectiveId());
        row.insert("subcards",responseIds(exact->getSubcards()));row.insert("legacy_string_key",exact->toString());
        row.insert("legacy_sort_key",legacySortKey(exact));row.insert("legacy_identity_token",aiLegacyCardIdentity(m_room,player,exact));
        row.insert("target_residue",responseResidueJson(responseResidue(m_room,player,exact)));
        row.insert("evaluation_rules",responseOwnerRules(m_room,player,exact));row.insert("locked",player->isLocked(exact));
        row.insert("using",exact->hasFlag("using"));row.insert("can_recast",exact->canRecast());
        row.insert("target_fixed",exact->targetFixed());row.insert("handling_method",int(exact->getHandlingMethod()));
        for(auto it=targets.begin();it!=targets.end();++it)row.insert(it.key(),it.value());
        if(!aiRound44RangeRequestCurrent(m_room,parentRequest))return fail("Jianman source constructor query changed state");
        return{{"exists",true},{"evaluation_only",true},{"card",row},{"candidate",targets}};
    }
    if(proposal.value("kind")=="faqi_saved_plan") {
        auto fail=[](const QString &why){return QJsonObject{{"error",why},{"key","faqiUse"}};};
        auto *player=m_room.findPlayerByObjectName(request.viewerObjectName);
        if(!player || !aiRound44RangeRequestCurrent(m_room,parentRequest))return fail("Faqi active request is stale");
        EngineRuntimeContextScope engine(*Sanguosha,&m_room);
        LuaRuntime::Binding game(m_room.roomRuntime()->lua(),false);
        ResponseNativeScope native(m_room,player,request.reason,request.pattern);
        const QString mode=proposal.value("mode").toString();
        QString error;
        if(mode=="capture") {
            if((proposal.size()!=5 && proposal.size()!=6) || (proposal.size()==6&&!proposal.value("source_fields").isObject())
                || !proposal.value("selector").isObject() || !proposal.value("targets").isArray()
                || !proposal.value("offered_id").isDouble())return fail("Faqi capture shape malformed");
            const double raw=proposal.value("offered_id").toDouble(-1);
            if(raw<0 || raw!=std::floor(raw) || raw>std::numeric_limits<int>::max())return fail("Faqi offered ID malformed");
            QStringList names;for(const auto &value:proposal.value("targets").toArray()){
                if(!value.isString()||value.toString().isEmpty())return fail("Faqi target malformed");names<<value.toString();}
            if(names.size()>128)return fail("Faqi target budget exceeded");
            const auto selector=proposal.value("selector").toObject();
            AIReadonlyCardLease lease;
            if (selector.size() == 1 && selector.value("readonly_recipe_key").isString()) {
                lease = m_room.roomRuntime()->ai().retainReadonlyCard(parentRequest,player,
                    selector.value("readonly_recipe_key").toString());
            } else if (selector.size() == 2 && selector.value("legacy_identity_token").isString()) {
                const QString identity = selector.value("legacy_identity_token").toString();
                const bool conversion = selector.contains("conversion_id");
                const auto value = selector.value(conversion ? "conversion_id" : "physical_id");
                const double raw = value.toDouble(-1);
                if (!value.isDouble() || raw < 0 || raw != std::floor(raw)
                    || raw > std::numeric_limits<int>::max() || identity.isEmpty())
                    return fail("Faqi observation identity is invalid");
                const int id = int(raw);
                if (conversion) {
                    const auto *ticket = responseTicket(request,id);
                    if (!ticket || ticket->activationRef.ownerObjectName != player->objectName()
                        || ticket->legacyIdentityToken != identity)
                        return fail("Faqi observation does not match issued conversion identity");
                    lease = request.conversionLedger->observedProducts.value(id);
                    const Card *exact = lease ? lease() : nullptr;
                    // A display/policy clone is not the original producer object.
                    // Never equate them by costs, class, or toString alone.
                    if (!exact || aiLegacyCardIdentity(m_room,player,exact) != identity
                        || exact->toString() != ticket->legacyStringKey)
                        return fail("Faqi conversion lacks matching exact producer custody");
                } else {
                    QList<AICardView> visible = request.worldView.handCards;
                    visible.append(request.worldView.discardPile);
                    visible.append(request.choiceOptions.cards);
                    QList<AIPlayerView> roster=request.worldView.players;roster.prepend(request.worldView.self);
                    for (const auto &p : roster) {
                        visible.append(p.equips);visible.append(p.judgingArea);visible.append(p.knownCards);
                        for (const auto &pile:p.piles)visible.append(pile.cards);
                    }
                    std::shared_ptr<const CardLifetimeToken> observed;
                    for (const auto &face:visible)
                        if (face.effectiveId==id && face.nativeIdentity && face.nativeIdentity->live
                            && aiLegacyCardIdentity(m_room,player,face.nativeIdentity)==identity) {
                            observed=face.nativeIdentity;break;
                        }
                    if (!observed) return fail("Faqi physical identity is not currently disclosed");
                    // getEquips returns the real inner card, while hand views use
                    // the outer WrappedCard. Authenticate the exact projected one.
                    const Card *outer=Sanguosha->getCard(id), *exact=nullptr;
                    if (outer && globalCardLifetimeManager().liveToken(outer)==observed)exact=outer;
                    else if (outer && globalCardLifetimeManager().liveToken(outer->getRealCard())==observed)exact=outer->getRealCard();
                    if (!exact || exact->isVirtualCard()) return fail("Faqi disclosed physical object changed");
                    lease=AIOwnerReadonlyCardStore::observe(exact);
                }
            } else return fail("Faqi observation selector is not a finite issued identity");
            const QString token=captureAIRound41FaqiPlan(request,player,int(raw),std::move(lease),names,&error,proposal.value("source_fields").toObject());
            if(token.isEmpty())return fail(error);return{{"token",token}};
        }
        if(mode=="select") {
            if(proposal.size()!=3||!proposal.value("token").isString())return fail("Faqi select shape malformed");
            return issueAIRound41FaqiReceipt(request,player,proposal.value("token").toString(),[&](CardUseStruct &use){
                return m_skillRuntime.resolveCardSkillInstance(use);
            });
        }
        if(mode!="construct" || proposal.size()!=4 || !proposal.value("constructor").isString() || !proposal.value("costs").isArray())
            return fail("Faqi source constructor shape malformed");
        QList<int> costs;for(const auto &value:proposal.value("costs").toArray()){
            const double id=value.toDouble(-1);if(!value.isDouble()||id<0||id!=std::floor(id)||id>std::numeric_limits<int>::max())return fail("Faqi constructor cost malformed");costs<<int(id);}
        auto card=constructAIRound41FaqiRevise(request,player,proposal.value("constructor").toString(),costs,&error);
        if(!error.isEmpty())return fail(error);if(!card)return{{"exists",false}};
        const auto key=m_room.roomRuntime()->ai().issueReadonlyCard(parentRequest,player,std::move(card),"faqi_limu_revise");
        const auto lease=m_room.roomRuntime()->ai().retainReadonlyCard(parentRequest,player,key);
        if(!lease||!lease())return fail("Faqi readonly constructor custody missing");
        const Card *exact=lease();int budget=4096;
        const auto candidate=makeAICardCandidate(m_room,player,exact,Card::MethodUse,AIRequest::Activate,QString(),budget);
        auto row=publicDecisionCard(exact,true);const auto targets=responseCandidateJson(candidate);
        row.insert("evaluation_only",true);row.insert("evaluation_owner",player->objectName());
        row.insert("readonly_recipe_key",key);row.insert("virtual_card",exact->isVirtualCard());
        row.insert("id",exact->getEffectiveId());row.insert("effective_id",exact->getEffectiveId());
        row.insert("subcards",responseIds(exact->getSubcards()));row.insert("legacy_string_key",exact->toString());
        row.insert("legacy_sort_key",legacySortKey(exact));row.insert("legacy_identity_token",aiLegacyCardIdentity(m_room,player,exact));
        row.insert("target_residue",responseResidueJson(responseResidue(m_room,player,exact)));
        row.insert("evaluation_rules",responseOwnerRules(m_room,player,exact));row.insert("locked",player->isLocked(exact));
        row.insert("using",exact->hasFlag("using"));row.insert("can_recast",exact->canRecast());
        row.insert("target_fixed",exact->targetFixed());row.insert("handling_method",int(exact->getHandlingMethod()));
        for(auto it=targets.begin();it!=targets.end();++it)row.insert(it.key(),it.value());
        if(!aiRound44RangeRequestCurrent(m_room,parentRequest))return fail("Faqi source constructor query changed state");
        return{{"exists",true},{"evaluation_only",true},{"card",row},{"candidate",targets}};
    }
    if (proposal.value("kind")=="observe_overseas_lihuo") {
        auto fail=[](const QString &why){return QJsonObject{{"error",why},{"key","ov_lihuo.hypothesis"}};};
        if(proposal.size()!=1)return fail("Lihuo source observation accepts no caller-authored recipe or targets");
        ServerPlayer *viewer=m_room.findPlayerByObjectName(request.viewerObjectName);
        EngineRuntimeContextScope engine(*Sanguosha,&m_room);
        LuaRuntime::Binding game(m_room.roomRuntime()->lua(),false);
        CardUseStruct use;QString error;
        auto card=createAIRound28CapturedLihuoHypothesis(request,m_room,viewer,&use,&error);
        if(!card)return fail(error);
        // Ownership goes into the existing readonly store; the observer retains
        // that same native lease, never a second owner or a copied old Slash.
        const auto key=m_room.roomRuntime()->ai().issueReadonlyCard(parentRequest,viewer,std::move(card),"ov_lihuo_hypothesis");
        const auto lease=m_room.roomRuntime()->ai().retainReadonlyCard(parentRequest,viewer,key);
        if(!lease||!lease())return fail("Lihuo readonly custody could not be retained");
        use.card=lease();
        if(!recordAIRound28LihuoDelegateUse(request,viewer,use,lease,&error))return fail(error);
        // Source wrote its tag before aiUseCard: subsequent projection/query
        // errors leave the reached observation intact, exactly like the donor.
        int budget=4096;
        const auto candidate=makeAICardCandidate(m_room,viewer,lease(),Card::MethodUse,AIRequest::Activate,QString(),budget);
        QJsonObject face=lihuoPolicyCard(lease());
        face.insert("evaluation_only",true);face.insert("evaluation_owner",viewer->objectName());
        face.insert("evaluation_rules",responseOwnerRules(m_room,viewer,lease()));
        face.insert("readonly_recipe_key",key);face.insert("virtual_card",lease()->isVirtualCard());
        face.insert("target_residue",responseResidueJson(responseResidue(m_room,viewer,lease())));
        face.insert("target_fixed",lease()->targetFixed());face.insert("handling_method",int(lease()->getHandlingMethod()));
        face.insert("can_recast",lease()->canRecast());face.insert("subcards",responseIds(lease()->getSubcards()));
        face.insert("locked",viewer->isLocked(lease()));face.insert("using",lease()->hasFlag("using"));
        const auto targetData=responseCandidateJson(candidate);
        for(auto it=targetData.begin();it!=targetData.end();++it)face.insert(it.key(),it.value());
        QStringList targets;for(auto *target:use.to)targets<<target->objectName();
        return{{"kind","overseas_lihuo_hypothesis"},{"viewer",request.viewerObjectName},
            {"decision_id",request.getDecisionId()},{"state_revision",request.getStateRevision()},
            {"destination","yb_zhuzhan2_data"},{"candidate",targetData},
            {"use",QJsonObject{{"from",viewer->objectName()},{"to",QJsonArray::fromStringList(targets)},{"card",face}}}};
    }
    if (proposal.value("kind").toString()=="evaluate_extra_shicai") {
        auto failure=[](const QString &why){return QJsonObject{{"error",why},{"key","guandu_shicai"}};};
        if(proposal.size()!=1)return failure("Shicai evaluation has no parameters");
        auto *player=m_room.findPlayerByObjectName(request.viewerObjectName);
        const Card *card=aiRound29ShicaiPreview(request,m_room,player);
        if(!card)return failure("Shicai top card is not currently witnessed");
        EngineRuntimeContextScope contextScope(*Sanguosha,&m_room);
        LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(),false);
        ResponseNativeScope responseScope(m_room,player,request.reason,request.pattern);
        int budget=4096;
        const auto candidate=makeAICardCandidate(m_room,player,card,Card::MethodUse,AIRequest::Activate,QString(),budget);
        const auto targets=responseCandidateJson(candidate),rules=responseOwnerRules(m_room,player,card);
        auto row=request.worldView.strategyFacts.value("extra29").toObject().value("shicai_preview").toObject().value("card").toObject();
        row.insert("legacy_identity_token",aiLegacyCardIdentity(m_room,player,card));
        row.insert("evaluation_only",true);row.insert("virtual_card",false);
        row.insert("evaluation_owner",player->objectName());row.insert("evaluation_rules",rules);
        row.insert("can_recast",card->canRecast());row.insert("handling_method",int(card->getHandlingMethod()));
        row.insert("locked",player->isLocked(card));
        row.insert("target_residue",responseResidueJson(responseResidue(m_room,player,card)));
        for(auto it=targets.begin();it!=targets.end();++it)row.insert(it.key(),it.value());
        if(!aiRound44RangeRequestCurrent(m_room,parentRequest))return failure("Shicai evaluation changed state");
        return {{"card",row},{"candidate",targets}};
    }
    if (proposal.value("kind").toString()=="bind_zhifeng_question") {
        const auto fail=[](const QString &why){return QJsonObject{{"error",why},{"key","zhifeng.question"}};};
        if(proposal.size()!=2 || !proposal.value("spec").isObject() || !request.conversionLedger)
            return fail("invalid Zhifeng question proposal");
        const auto spec=proposal.value("spec").toObject();const auto ids=spec.value("subcards").toArray();
        if(spec.size()!=4 || !spec.value("name").isString() || !spec.value("suit").isDouble()
            || !spec.value("number").isDouble() || !spec.value("subcards").isArray() || ids.size()!=1
            || !ids.first().isDouble() || ids.first().toDouble()<0
            || ids.first().toDouble()!=std::floor(ids.first().toDouble())
            || ids.first().toDouble()>std::numeric_limits<int>::max())return fail("invalid Zhifeng source recipe");
        ServerPlayer *player=m_room.findPlayerByObjectName(request.viewerObjectName);
        EngineRuntimeContextScope engine(*Sanguosha,&m_room);LuaRuntime::Binding game(m_room.roomRuntime()->lua(),false);
        ResponseNativeScope native(m_room,player,request.reason,request.pattern);
        QString error;std::unique_ptr<Card> card(createAIRound29ZhifengProduct(request,m_room,player,
            request.zhifengQuestionSerial,{ids.first().toInt()},&error));
        if(!card)return fail(error);
        if(card->objectName()!=spec.value("name").toString() || double(card->getSuit())!=spec.value("suit").toDouble()
            || double(card->getNumber())!=spec.value("number").toDouble()) {
            auto out=fail("selected Zhifeng source face differs from its native product");out.insert("native_rejected",true);return out;
        }
        AICardConversionView t;t.zhifengQuestion=true;t.zhifengQuestionSerial=request.zhifengQuestionSerial;
        t.conversionId=request.cardConversions.size()+request.conversionLedger->tickets.size()+1;
        t.producerVersion=1;t.producerReason=request.reason;t.producerPattern=request.pattern;t.policyClass="@zhifeng_question";
        t.name=card->objectName();t.className=card->getClassName();t.suit=int(card->getSuit());t.number=card->getNumber();
        t.skillName=card->getSkillName(false);t.legacyStringKey=card->toString();t.subcardIds=card->getSubcards();t.costCount=1;
        t.available=true;t.matchesResponsePattern=true;request.conversionLedger->tickets<<t;
        const QJsonObject out{{"conversion_id",t.conversionId},{"name",t.name},{"suit",t.suit},{"number",t.number},{"subcards",ids}};
        return {{"kind","zhifeng_native_question"},{"class_name",t.className},{"card_spec",out},
            {"viewer",request.viewerObjectName},{"decision_id",request.getDecisionId()},{"state_revision",request.getStateRevision()}};
    }
    if (proposal.value("kind").toString()=="rangyi_question") {
        const auto fail=[](const QString&why){return QJsonObject{{"error",why},{"key","twyj_rangyi.question"}};};
        if(!proposal.value("input").isObject()||proposal.size()!=2||!request.conversionLedger)return fail("Rangyi query schema invalid");
        const QJsonObject input=proposal.value("input").toObject();const QString op=input.value("op").toString();
        ServerPlayer*player=m_room.findPlayerByObjectName(request.viewerObjectName);
        EngineRuntimeContextScope engine(*Sanguosha,&m_room);LuaRuntime::Binding game(m_room.roomRuntime()->lua(),false);
        QString error;
        if(op=="prepare") {
            const auto v=input.value("id");const double d=v.toDouble(-1);
            if(input.size()!=2||!v.isDouble()||d<0||d!=std::floor(d)||d>std::numeric_limits<int>::max())return fail("Rangyi source material malformed");
            std::unique_ptr<Card> card(createAIRound52RangyiProduct(request,m_room,player,int(d),false,&error));
            if(!card)return fail(error);
            const QString key=m_room.roomRuntime()->ai().issueReadonlyCard(parentRequest,player,std::move(card),"round52_rangyi_source");
            if(key.isEmpty()||!aiRound52RangyiCurrent(request,m_room,player,int(d)))return fail("Rangyi source clone custody expired");
            return {{"kind","rangyi_readonly"},{"key",key}};
        }
        if(op!="select"||input.size()!=2||!input.value("key").isString())return fail("Rangyi selected schema invalid");
        const QString key=input.value("key").toString();auto&runtime=m_room.roomRuntime()->ai();
        if(runtime.readonlyCardSource(parentRequest,player,key)!="round52_rangyi_source")return fail("Rangyi selected source custody missing");
        const auto lease=runtime.retainReadonlyCard(parentRequest,player,key);const Card*source=lease?lease():nullptr;
        if(!source||source->objectName()!="twyj_rangyiUse"||source->getSkillName(false)!="twyj_rangyiuse"||source->getSubcards().size()!=1)return fail("Rangyi selected source changed");
        const int id=source->getSubcards().first();
        std::unique_ptr<Card> card(createAIRound52RangyiProduct(request,m_room,player,id,true,&error));
        if(!card)return fail(error);
        if(lease()!=source||source->toString()!=card->toString()||source->getClassName()!=card->getClassName())return fail("Rangyi clone and selected product disagree");
        AICardConversionView t;t.rangyiQuestion=true;t.rangyiQuestionSerial=request.round52RangyiQuestionSerial;
        t.conversionId=request.cardConversions.size()+request.conversionLedger->tickets.size()+1;
        t.producerVersion=0;t.producerReason=request.reason;t.producerPattern=request.pattern;t.policyClass="@rangyi_question";
        t.name=card->objectName();t.className=card->getClassName();t.suit=int(card->getSuit());t.number=card->getNumber();
        t.skillName=card->getSkillName(false);t.legacyStringKey=card->toString();t.subcardIds=card->getSubcards();t.costCount=1;
        t.available=true;t.matchesResponsePattern=true;request.conversionLedger->tickets<<t;
        // No activation/source ref, quota or borrowed instance: final Room
        // resolver and the actual raw LuaSkillCard target rules remain intact.
        return {{"kind","rangyi_native_question"},{"viewer",request.viewerObjectName},
          {"decision_id",request.getDecisionId()},{"state_revision",request.getStateRevision()},
          {"card_spec",QJsonObject{{"conversion_id",t.conversionId},{"name",t.name},{"suit",t.suit},{"number",t.number},{"subcards",QJsonArray{id}}}}};
    }
    if (proposal.value("kind").toString() == "bind_jieyingh_question") {
        const auto fail = [](const QString &why) { return QJsonObject{{"error", why}, {"key", "jieyingh.question"}}; };
        if (proposal.size() != 1 || !request.conversionLedger)
            return fail("Jieyingh question binder accepts no product, costs or declaration");
        ServerPlayer *player = m_room.findPlayerByObjectName(request.viewerObjectName);
        EngineRuntimeContextScope engine(*Sanguosha, &m_room);
        LuaRuntime::Binding game(m_room.roomRuntime()->lua(), false);
        QString error;
        std::unique_ptr<Card> card(createAIRound27JieyinghProduct(request, m_room, player,
            request.jieyinghQuestionSerial, &error));
        if (!card) return fail(error);
        AICardConversionView ticket;
        ticket.jieyinghQuestion = true; ticket.jieyinghQuestionSerial = request.jieyinghQuestionSerial;
        ticket.conversionId = request.cardConversions.size() + request.conversionLedger->tickets.size() + 1;
        ticket.producerVersion = 1; ticket.producerReason = request.reason; ticket.producerPattern = request.pattern;
        ticket.policyClass = "@jieyingh_question";
        ticket.name = card->objectName(); ticket.className = card->getClassName();
        ticket.suit = int(card->getSuit()); ticket.number = card->getNumber();
        ticket.skillName = card->getSkillName(false); ticket.legacyStringKey = card->toString();
        ticket.available = true; ticket.matchesResponsePattern = true;
        // Leave activation/source refs and quota booleans unset: this receipt
        // represents an actual native question, never a borrowed skill instance.
        request.conversionLedger->tickets << ticket;
        const QJsonObject spec{{"conversion_id", ticket.conversionId}, {"name", ticket.name},
            {"suit", ticket.suit}, {"number", ticket.number}, {"subcards", QJsonArray()}};
        return {{"kind", "jieyingh_native_question"}, {"class_name", ticket.className},
            {"card_spec", spec}, {"viewer", request.viewerObjectName},
            {"decision_id", request.getDecisionId()}, {"state_revision", request.getStateRevision()}};
    }
    if (proposal.value("kind").toString() == "response_catalogue") {
        const QString key=proposal.value("key").toString();
        if(key=="sfofl_zhengan_basic_printed" || key=="ny_tenth_jvewu_printed"
           || key=="sfofl_zhengan_basic_set" || key=="sfofl_zhengan_basic_available")
            return resolveAIRound39PublicDeck(m_room,
                m_room.findPlayerByObjectName(request.viewerObjectName), request, proposal);
        return resolveAIResponseCatalogue(m_room,
            m_room.findPlayerByObjectName(request.viewerObjectName), request, proposal);
    }

    if (proposal.value("kind").toString() == QStringLiteral("pattern_log_name")) {
        const auto reject = [](const char *reason) {
            return QJsonObject{{"error", QString::fromLatin1(reason)}, {"key", "pattern_log_name"}};
        };
        ServerPlayer *viewer = m_room.findPlayerByObjectName(request.viewerObjectName);
        const bool derivative = proposal.value("derivative").isBool() && proposal.value("derivative").toBool();
        if (proposal.size() != (derivative ? 3 : 2) || !proposal.value("pattern").isString() || !viewer
            || request.worldView.self.objectName != viewer->objectName()
            || !aiRound44RangeRequestCurrent(m_room,parentRequest)) return reject("unauthorized pattern-log query");
        // Fixed OverseasVersion-ai512: PatternsCard(offeredName,nil,true).
        // The configured printed catalogue is public; no live draw order is read.
        if (derivative && (request.kind != AIRequest::Choice || request.choiceOptions.reason != "ov_weipo"
            || !viewer->hasSkill("ov_weipo") || !request.choiceOptions.choices.contains(proposal.value("pattern").toString())))
            return reject("derivative sample is outside the actual Weipo offered choice");
        if (!request.conversionLedger || ++request.conversionLedger->queryCount > 128)
            return reject("pattern-log query budget exceeded");
        EngineRuntimeContextScope engineScope(*Sanguosha, &m_room);
        LuaRuntime::Binding game(m_room.roomRuntime()->lua(), false);
        return sampleAIStrategyPatternLogName(m_room, viewer, proposal.value("pattern").toString(), derivative);
    }
    if (proposal.value("kind").toString() == QStringLiteral("deck_count_shuffle")) {
        const auto reject = [](const char *reason) {
            return QJsonObject{{"error", QString::fromLatin1(reason)}, {"key", "deck_count_shuffle"}};
        };
        ServerPlayer *viewer = m_room.findPlayerByObjectName(request.viewerObjectName);
        if (proposal.size() != 1 || !viewer || viewer->getRoom() != &m_room
            || request.worldView.self.objectName != viewer->objectName()
            || !aiRound44RangeRequestCurrent(m_room,parentRequest))
            return reject("stale or unauthorized deck-count query");
        if (!request.conversionLedger || ++request.conversionLedger->queryCount > 128)
            return reject("deck-count query budget exceeded");
        return consumeAIStrategyDeckCountShuffle(m_room, viewer);
    }
    if (proposal.value("kind").toString() == QStringLiteral("random_own_hand")) {
        const auto reject = [](const char *message) {
            return QJsonObject{{"error", message}, {"key", "random_own_hand"}};
        };
        ServerPlayer *viewer = m_room.findPlayerByObjectName(request.viewerObjectName);
        if (proposal.size() != 1 || !viewer || viewer->getRoom() != &m_room
            || request.worldView.self.objectName != viewer->objectName()
            || !aiRound44RangeRequestCurrent(m_room,parentRequest)
            || request.kind != AIRequest::RespondCard || request.choiceOptions.question != "askForCardShow")
            return reject("stale or unauthorized owner-hand sample");
        if (!request.conversionLedger || ++request.conversionLedger->queryCount > 128)
            return reject("owner-hand sample query budget exceeded");
        const QList<int> ids = viewer->handCards();
        QList<int> disclosed;
        for (const AICardView &card : request.worldView.handCards) disclosed.append(card.effectiveId);
        // getRandomHandCard samples the original live hand order. A subset or
        // reordered snapshot cannot describe that distribution or RNG mapping.
        if (ids != disclosed) return reject("owner-hand sample domain changed");
        QJsonObject result{{"empty", ids.isEmpty()}};
        if (!ids.isEmpty()) result.insert("card_id", ids.at(m_room.roomRuntime()->rng().bounded(ids.size())));
        return result;
    }
    if (proposal.value("kind").toString() == QStringLiteral("random_hand_discard"))
        return resolveAIStrategyRandomPlanningQuery(m_room,
            m_room.findPlayerByObjectName(request.viewerObjectName), request, proposal);
    if (proposal.value("kind")=="ordinary_card_choice_prefix") {
        auto *viewer=m_room.findPlayerByObjectName(request.viewerObjectName);
        if (!viewer || !request.conversionLedger || !aiRound44RangeRequestCurrent(m_room,parentRequest)
            || ++request.conversionLedger->queryCount>128)
            return {{"error","ordinary card-choice prefix authority unavailable"},{"key","askForCardChosen.prefix"}};
        QJsonObject input=proposal;input.remove("kind");
        return queryAIRound54CardChoicePrefix(m_room,viewer,request,input);
    }
    if (proposal.value("kind").toString() == QStringLiteral("refresh_discard_preferences")
        || proposal.value("kind").toString() == QStringLiteral("ordinary_card_choice_preferences")) {
        const auto reject = [](const char *reason) {
            return QJsonObject{{"error", QString::fromLatin1(reason)}, {"key", "discard_preferences"}};
        };
        ServerPlayer *viewer = m_room.findPlayerByObjectName(request.viewerObjectName);
        if (!viewer || viewer->getRoom() != &m_room || !viewer->isAlive()
            || request.worldView.self.objectName != viewer->objectName()
            || !aiRound44RangeRequestCurrent(m_room,parentRequest))
            return reject("stale or unauthorized discard-preference request");
        if (!request.conversionLedger || ++request.conversionLedger->queryCount > 128)
            return reject("discard-preference query budget exceeded");
        if (proposal.size() != 1)
            return reject("discard-preference refresh accepts no arguments");
        if (proposal.value("kind")=="ordinary_card_choice_preferences")
            return refreshAIRound54OrdinaryCardChoice(m_room,viewer,request);
        return refreshAIStrategyDiscardPreferences(m_room, viewer);
    }
    const auto failure = [](const QString &reason) {
        return QJsonObject{{"error", reason}, {"key", "conversion_policy"}};
    };
    const auto rejectedPayment = [](const QString &reason) {
        return QJsonObject{{"error", reason}, {"key", "conversion_policy"}, {"native_rejected", true}};
    };
    if (!request.conversionLedger || request.conversionLedger->tickets.size() >= 128)
        return failure("conversion authority query budget exhausted");
    if (!aiRound44RangeRequestCurrent(m_room,parentRequest))
        return failure("conversion authority request is stale");
    ServerPlayer *player = m_room.findPlayerByObjectName(request.viewerObjectName);
    if (!player || !player->isAlive()) return failure("conversion viewer is absent");
    if (proposal.value("kind") == "evaluate_response_policy") {
        const auto scopeValue=proposal.value("scope_id");
        const double rawScope=scopeValue.toDouble(-1);
        if (proposal.size()!=7 || !scopeValue.isDouble() || rawScope<1
            || rawScope!=std::floor(rawScope) || rawScope>std::numeric_limits<int>::max()
            || !proposal.value("class_name").isString() || !proposal.value("registry").isString()
            || !proposal.value("hook_key").isString() || !proposal.value("attach_request").isBool())
            return failure("readonly response parser scope is malformed");
        const int scopeId=int(rawScope);
        const auto scope=request.conversionPolicy.value("scopes").toObject().value(QString::number(scopeId)).toObject();
        if (scope.isEmpty() || scope.value("skill_action").toBool() || scope.value("play").toBool()
            || scope.value("class_name")!=proposal.value("class_name"))
            return failure("readonly response callback scope was not issued");
        const auto identity=scope.value("identity").toObject();
        const auto activation=responseRef(identity,"activation"), source=responseRef(identity,"source");
        if (!activation.isValid() || !source.isValid() || activation.ownerObjectName!=player->objectName()
            || !player->hasSkillInstance(activation.key.skillName,activation.key.instanceID))
            return failure("readonly response activation identity changed");
        auto root=m_skillRuntime.resolveSkillInstanceRootRef(activation);
        if (!root.isValid()) root=activation;
        if (root!=source) return failure("readonly response source identity changed");
        const auto *skill=Sanguosha->getViewAsSkill(scope.value("skill_name").toString());
        if (!skill || (dynamic_cast<const ViewAsSkillV2 *>(skill)?2:1)!=scope.value("version").toInt())
            return failure("readonly response source prototype changed");
        EngineRuntimeContextScope contextScope(*Sanguosha,&m_room);
        LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(),false);
        ResponseNativeScope responseScope(m_room,player,request.reason,request.pattern);
        const auto objectSpec=proposal.value("spec").toObject();
        const QString objectKey=objectSpec.value("source_object_key").toString();
        const QString objectSource=QStringLiteral("r33_cardsview:%1").arg(scopeId);
        const Card *ownedObject=nullptr;
        if (objectSpec.contains("source_object_key")) {
            if (m_room.roomRuntime()->ai().readonlyCardSource(parentRequest,player,objectKey)!=objectSource)
                return failure("Card-object recipe belongs to another callback scope");
            ownedObject=m_room.roomRuntime()->ai().readonlyCard(parentRequest,player,objectKey);
            if (!ownedObject) return failure("Card-object readonly recipe expired");
        }
        auto sourceObject=buildAIRound33CardsViewObject(request,m_room,player,scope,
            proposal.value("registry").toString(),proposal.value("hook_key").toString(),
            objectSpec,proposal.value("attach_request").toBool(),ownedObject);
        AIRound30ResponsePolicy product;
        if (sourceObject.handled) {
            if (!sourceObject.error.isEmpty()) return failure(sourceObject.error);
            product.status=sourceObject.absent?AIRound30ResponsePolicy::Absent:AIRound30ResponsePolicy::Parsed;
            product.card=std::move(sourceObject.created);product.physicalCard=sourceObject.attached;
            product.bindingSpec=sourceObject.bindingSpec;
        } else product=buildAIRound30ResponsePolicy(request,m_room,player,scope,
            proposal.value("registry").toString(),proposal.value("hook_key").toString(),
            proposal.value("spec"),proposal.value("attach_request").toBool());
        if (product.status==AIRound30ResponsePolicy::Unknown) return failure(product.error);
        QJsonObject result{{"kind","response_policy_evaluation"},{"known",true},
            {"exists",product.status==AIRound30ResponsePolicy::Parsed},{"scope_id",scopeId},
            {"class_name",proposal.value("class_name")},{"registry",proposal.value("registry")},
            {"hook_key",proposal.value("hook_key")},{"attach_request",proposal.value("attach_request")}};
        if (product.status==AIRound30ResponsePolicy::Absent) return result;
        const Card *card=product.card ? product.card.get() : product.physicalCard;
        if (!card) return failure("readonly response parser lost its product");
        QJsonObject row=publicDecisionCard(card,true);
        if (sourceObject.handled) {
            row.insert("activation_skill",card->getActivationSkillName());
            row.insert("activation_instance",card->getActivationSkillInstanceId());
            row.insert("source_skill",card->getSourceSkillName());
            row.insert("source_instance",card->getSourceSkillInstanceId());
            if (sourceObject.attached) {
                row.insert("activation_owner",identity.value("activation_owner"));
                row.insert("source_owner",identity.value("source_owner"));
            }
        }
        const QString selector=product.bindingSpec.value("legacy_class").toString();
        if (selector.startsWith('#')) row.insert("legacy_selector",selector);
        QString sourcePattern=request.pattern;
        if (sourcePattern.endsWith('!')) sourcePattern.chop(1);
        if ((request.kind==AIRequest::UseCard || request.kind==AIRequest::RespondCard) && !sourcePattern.isEmpty())
            row.insert("matches_response_pattern",Sanguosha->matchPattern(sourcePattern,player,card));
        row.insert("virtual_card",card->isVirtualCard()); row.insert("effective_id",card->getEffectiveId());
        row.insert("id",card->getId()); row.insert("subcards",responseIds(card->getSubcards()));
        row.insert("legacy_string_key",card->toString()); row.insert("legacy_sort_key",legacySortKey(card));
        row.insert("locked",player->isLocked(card)); row.insert("jilei",player->isJilei(card));
        row.insert("using",card->hasFlag("using")); row.insert("can_recast",card->canRecast());
        row.insert("handling_method",int(card->getHandlingMethod())); row.insert("target_fixed",card->targetFixed());
        if (product.card || sourceObject.attached) {
            int budget=4096;
            const auto candidate=makeAICardCandidate(m_room,player,card,request.handlingMethod,request.kind,request.pattern,budget);
            const auto rules=responseOwnerRules(m_room,player,card);
            const auto targets=responseCandidateJson(candidate);
            row.insert("evaluation_only",true); row.insert("evaluation_owner",player->objectName());
            row.insert("evaluation_rules",rules);
            row.insert("target_residue",responseResidueJson(responseResidue(m_room,player,card)));
            for (auto it=targets.begin();it!=targets.end();++it) row.insert(it.key(),it.value());
            if (card->getTypeId()==Card::TypeSkill) {
                QJsonObject planning{{"exists",false}};
                std::unique_ptr<Card> proxy(Sanguosha->cloneCard(proposal.value("class_name").toString(),Card::NoSuit,0));
                if (proxy) {
                    proxy->setSkillName(card->getSkillName());
                    if (!card->willThrow()) proxy->addSubcards(card->getSubcards());
                    const auto proxyCandidate=makeAICardCandidate(m_room,player,proxy.get(),request.handlingMethod,request.kind,request.pattern,budget);
                    const auto proxyTargets=responseCandidateJson(proxyCandidate);
                    auto face=publicDecisionCard(proxy.get(),true);
                    face.insert("evaluation_only",true); face.insert("evaluation_owner",player->objectName());
                    face.insert("virtual_card",proxy->isVirtualCard()); face.insert("can_recast",proxy->canRecast());
                    // Complete same detached class-dummy policy face. Its costs
                    // come only from the already-authorized source prototype.
                    face.insert("id",proxy->getId()); face.insert("effective_id",proxy->getEffectiveId());
                    face.insert("subcards",responseIds(proxy->getSubcards()));
                    face.insert("legacy_string_key",proxy->toString()); face.insert("legacy_sort_key",legacySortKey(proxy.get()));
                    face.insert("using",proxy->hasFlag("using")); face.insert("jilei",player->isJilei(proxy.get()));
                    face.insert("target_fixed",proxy->targetFixed());
                    face.insert("handling_method",int(proxy->getHandlingMethod()));
                    face.insert("locked",player->isLocked(proxy.get()));
                    face.insert("evaluation_rules",responseOwnerRules(m_room,player,proxy.get()));
                    face.insert("target_residue",responseResidueJson(responseResidue(m_room,player,proxy.get())));
                    for (auto it=proxyTargets.begin();it!=proxyTargets.end();++it) face.insert(it.key(),it.value());
                    const bool patternMatch=Sanguosha->matchPattern(sourcePattern,player,proxy.get());
                    const bool limited=player->isCardLimited(proxy.get(),request.handlingMethod);
                    const auto proxyKey=m_room.roomRuntime()->ai().issueReadonlyCard(
                        parentRequest,player,std::move(proxy),QStringLiteral("response_policy_class"));
                    if(proxyKey.isEmpty())return failure("readonly response class proxy custody failed");
                    face.insert("readonly_recipe_key",proxyKey);
                    planning={{"exists",true},{"card",face},{"candidate",proxyTargets},
                        {"source_pattern_match",patternMatch},{"source_limited",limited}};
                }
                row.insert("response_class_planning",planning);
            }
            if (!aiRound44RangeRequestCurrent(m_room,parentRequest)) return failure("readonly response evaluation changed game state");
            const auto key=sourceObject.attached?objectKey:m_room.roomRuntime()->ai().issueReadonlyCard(
                parentRequest,player,std::move(product.card),sourceObject.handled?objectSource:QStringLiteral("response_policy"));
            if (key.isEmpty()) return failure("readonly response custody is unavailable");
            if (sourceObject.handled) result.insert("source_object_phase",sourceObject.attached?"attached":"created");
            row.insert("readonly_recipe_key",key);
            result.insert("evaluation_only",true); result.insert("candidate",targets); result.insert("card_rules",rules);
        } else result.insert("physical",true);
        result.insert("card",row); result.insert("binding_spec",product.bindingSpec);
        return result;
    }
    if(proposal.value("kind").toString()=="allocate_olclan_policy_card") {
        const auto v=proposal.value("conversion_id");const double raw=v.toDouble(-1);
        const QString form=proposal.value("policy_form").toString();
        if(proposal.size()!=3||(form!="ordinary"&&form!="legacy_response")||!v.isDouble()||raw<1||raw!=std::floor(raw)||raw>std::numeric_limits<int>::max()/2
            ||!needsOlClanObservationCustody(request,player))
            return failure("OlClan policy allocation requires exact saved-plan source and issued identity");
        const int id=int(raw);const auto *ticket=responseTicket(request,id);
        if(!ticket||ticket->activationRef.ownerObjectName!=player->objectName()
            ||(ticket->className!="GodSalvation"&&ticket->policyClass!="GodSalvation"))
            return failure("OlClan policy allocation is limited to the reachable GodSalvation replacement");
        // Negative IDs are native-only policy templates; positive IDs retain
        // authority products. Neither map entry is exposed as Lua authority.
        auto lease=request.conversionLedger->observedProducts.value(form=="legacy_response" ? -2*id+1 : -2*id);
        if(!lease&&form=="ordinary")lease=request.conversionLedger->observedProducts.value(id);
        const Card *source=lease?lease():nullptr;
        if(!source||!source->isVirtualCard())return failure("OlClan issued virtual policy template expired");
        EngineRuntimeContextScope contextScope(*Sanguosha,&m_room);
        LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(),false);
        ResponseNativeScope responseScope(m_room,player,request.reason,request.pattern);
        // All material IDs were certified by this unchanged request's producer.
        // The caller supplies neither a wire nor an alternate material sequence.
        const QString printed=source->toString();
        const Card *allocated=Card::Parse(printed);
        // Never adopt a borrowed physical return or the template itself.
        if(!allocated||!allocated->isVirtualCard()||allocated==source)
            return failure("OlClan source Card::Parse did not allocate a fresh virtual object");
        std::unique_ptr<Card> parsed(const_cast<Card *>(allocated));
        if(parsed->toString()!=printed)
            return failure("OlClan source Card::Parse did not preserve its issued virtual template");
        QJsonObject row=publicDecisionCard(parsed.get(),true);
        row.insert("evaluation_only",true);row.insert("virtual_card",true);
        row.insert("subcards",responseIds(parsed->getSubcards()));
        row.insert("effective_id",parsed->getEffectiveId());
        row.insert("legacy_string_key",parsed->toString());row.insert("legacy_sort_key",legacySortKey(parsed.get()));
        const QString recipe=m_room.roomRuntime()->ai().issueReadonlyCard(parentRequest,player,std::move(parsed),"olclan_godsalvation_policy");
        if(recipe.isEmpty())return failure("OlClan policy allocation lacks active readonly custody");
        row.insert("readonly_recipe_key",recipe);
        return {{"card",row}};
    }
    if (proposal.value("kind").toString() == "capture_olclan_plan"
        || proposal.value("kind").toString() == "restore_olclan_plan") {
        const bool capture = proposal.value("kind").toString() == "capture_olclan_plan";
        const QString slot = proposal.value("slot").toString();
        if (proposal.size() != 3 || !proposal.value("slot").isString())
            return failure("OlClan observation shape is invalid");
        if (nestedPlay && (!capture || slot != QStringLiteral("zuchengqiUse") || !player->hasSkill("zuchengqi")))
            return failure("nested source planning may retain only its actual Chengqi observation");
        EngineRuntimeContextScope contextScope(*Sanguosha,&m_room);
        LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(),false);
        ResponseNativeScope responseScope(m_room,player,request.reason,request.pattern);
        QString error;
        if (capture) {
            if (!proposal.value("selector").isObject()) return failure("OlClan observation selector is invalid");
            const auto selector = proposal.value("selector").toObject();
            AIReadonlyCardLease lease;
            if (selector.size() == 1 && selector.value("readonly_recipe_key").isString()) {
                lease = m_room.roomRuntime()->ai().retainReadonlyCard(parentRequest,player,
                    selector.value("readonly_recipe_key").toString());
            } else if (selector.size() == 2 && selector.value("legacy_identity_token").isString()) {
                const QString identity = selector.value("legacy_identity_token").toString();
                const bool conversion = selector.contains("conversion_id");
                const auto value = selector.value(conversion ? "conversion_id" : "physical_id");
                const double raw = value.toDouble(-1);
                if (!value.isDouble() || raw < 0 || raw != std::floor(raw)
                    || raw > std::numeric_limits<int>::max() || identity.isEmpty())
                    return failure("OlClan observation identity is invalid");
                const int id = int(raw);
                if (conversion) {
                    const auto *ticket = responseTicket(request,id);
                    if (!ticket || ticket->activationRef.ownerObjectName != player->objectName()
                        || ticket->legacyIdentityToken != identity)
                        return failure("OlClan observation does not match issued conversion identity");
                    lease = request.conversionLedger->observedProducts.value(id);
                    const Card *exact = lease ? lease() : nullptr;
                    // A display/policy clone is not the original producer object.
                    // Never equate them by costs, class, or toString alone.
                    if (!exact || aiLegacyCardIdentity(m_room,player,exact) != identity
                        || exact->toString() != ticket->legacyStringKey)
                        return failure("OlClan conversion lacks matching exact producer custody");
                } else {
                    QList<AICardView> visible = request.worldView.handCards;
                    visible.append(request.worldView.discardPile);
                    visible.append(request.choiceOptions.cards);
                    QList<AIPlayerView> roster=request.worldView.players;roster.prepend(request.worldView.self);
                    for (const auto &p : roster) {
                        visible.append(p.equips);visible.append(p.judgingArea);visible.append(p.knownCards);
                        for (const auto &pile:p.piles)visible.append(pile.cards);
                    }
                    std::shared_ptr<const CardLifetimeToken> observed;
                    for (const auto &face:visible)
                        if (face.effectiveId==id && face.nativeIdentity && face.nativeIdentity->live
                            && aiLegacyCardIdentity(m_room,player,face.nativeIdentity)==identity) {
                            observed=face.nativeIdentity;break;
                        }
                    if (!observed) return failure("OlClan physical identity is not currently disclosed");
                    // getEquips returns the real inner card, while hand views use
                    // the outer WrappedCard. Authenticate the exact projected one.
                    const Card *outer=Sanguosha->getCard(id), *exact=nullptr;
                    if (outer && globalCardLifetimeManager().liveToken(outer)==observed)exact=outer;
                    else if (outer && globalCardLifetimeManager().liveToken(outer->getRealCard())==observed)exact=outer->getRealCard();
                    if (!exact || exact->isVirtualCard()) return failure("OlClan disclosed physical object changed");
                    lease=AIOwnerReadonlyCardStore::observe(exact);
                }
            } else return failure("OlClan observation selector is not a finite issued identity");
            const QString token=captureAIRound23OlClanObservation(request,player,slot,std::move(lease),&error);
            if(token.isEmpty())return failure(error);
            return {{"token",token}};
        }
        if (!proposal.value("token").isString()) return failure("OlClan observation token is invalid");
        const Card *card=restoreAIRound23OlClanObservation(request,player,slot,proposal.value("token").toString(),&error);
        if(!card)return failure(error);
        // Deliberately no candidate, receipt, recipe key, availability or rules.
        // Foreign public physical cards keep only their public activation chain.
        const bool privateOwner = card->isVirtualCard() || m_room.getCardOwner(card->getEffectiveId())==player;
        QJsonObject row=publicDecisionCard(card,privateOwner);
        row.insert("evaluation_only",true);
        row.insert("virtual_card",card->isVirtualCard());
        row.insert("effective_id",card->getEffectiveId());
        row.insert("subcards",responseIds(card->getSubcards()));
        row.insert("legacy_identity_token",aiLegacyCardIdentity(m_room,player,card));
        row.insert("legacy_string_key",card->toString());
        row.insert("legacy_sort_key",legacySortKey(card));
        if(!aiRound44RangeRequestCurrent(m_room,parentRequest))return failure("OlClan observation changed state");
        return {{"card",row}};
    }
    if (proposal.value("kind").toString() == "evaluate_preview_card"
        || proposal.value("kind").toString() == "bind_preview_physical") {
        const bool selected = proposal.value("kind").toString() == "bind_preview_physical";
        const auto idValue = proposal.value("card_id"); const double rawId = idValue.toDouble(-1);
        if (proposal.size() != (selected ? 3 : 2) || !idValue.isDouble() || rawId < 0
            || rawId != std::floor(rawId) || rawId > std::numeric_limits<int>::max())
            return failure("preview card identity is malformed");
        const int id = int(rawId); QJsonObject disclosed;
        if (!authorizeAIStrategyPreviewEvaluation(request, m_room, player, id, &disclosed)
            && !authorizeAIRound45NotifiedEvaluation(request,m_room,player,id,&disclosed)
            && !authorizeAIRound49NotifiedEvaluation(request,m_room,player,id,&disclosed))
            return failure("preview card lacks current recipient disclosure");
        EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
        LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
        ResponseNativeScope responseScope(m_room, player, request.reason, request.pattern);
        const Card *card = Sanguosha->getCard(id);
        if (!card || card->isVirtualCard()) return failure("preview physical identity changed");
        if (selected) {
            const auto scopeValue = proposal.value("scope_id"); const double scopeNumber = scopeValue.toDouble(-1);
            if (!scopeValue.isDouble() || scopeNumber < 0 || scopeNumber != std::floor(scopeNumber)
                || scopeNumber > std::numeric_limits<int>::max()) return failure("preview scope is malformed");
            const auto scope = request.conversionPolicy.value("scopes").toObject().value(QString::number(int(scopeNumber))).toObject();
            const auto identity = scope.value("identity").toObject();
            const auto activation = responseRef(identity, "activation"), source = responseRef(identity, "source");
            if (!scope.value("skill_action").toBool() || scope.value("class_name") != "@skill_action"
                || (scope.value("skill_name") != "caoduo" && scope.value("skill_name").toString()!=aiRound45NotifiedSkill(request) && scope.value("skill_name").toString()!=aiRound49NotifiedSkill(request)) || scope.value("reason").toInt() != request.reason
                || scope.value("pattern").toString() != request.pattern
                || (!authorizeAIStrategyPreviewPhysicalProduct(request, m_room, player, activation, source, id)
                    && !authorizeAIRound45NotifiedPhysical(request,m_room,player,activation,source,id)
                    && !authorizeAIRound49NotifiedPhysical(request,m_room,player,activation,source,id)))
                return failure("preview physical product lacks exact selected authority");
            if (player->isCardLimited(card, request.handlingMethod)) return rejectedPayment("preview physical card limited");
            AiSkillActionContext receipt; receipt.activationRef = activation; receipt.sourceRef = source;
            request.conversionLedger->previewPhysicalReceipts.insert(id, receipt);
            return {{"accepted", true}, {"card_id", id}};
        }
        int budget = 4096;
        // Donor aiUseCard plans the original card before its caoduo-flag test.
        const auto candidate = makeAICardCandidate(m_room, player, card, Card::MethodUse, AIRequest::Activate, QString(), budget);
        const auto targets = responseCandidateJson(candidate), rules = responseOwnerRules(m_room, player, card);
        auto row = disclosed; row.insert("flags_complete", false);
        row.insert("legacy_identity_token", aiLegacyCardIdentity(m_room, player, card));
        // Physical Card::toString is the already disclosed ID, not a private
        // skill/flag serialization. Full source valuation sorts by this key.
        row.insert("legacy_string_key", card->toString());
        if (!card->isKindOf("ActiveSkillCard"))
            row.insert("legacy_sort_key", card->toString());

        row.insert("evaluation_only", true); row.insert("evaluation_owner", player->objectName());
        row.insert("virtual_card", false); row.insert("evaluation_rules", rules);
        row.insert("can_recast", card->canRecast()); row.insert("handling_method", int(card->getHandlingMethod()));
        row.insert("locked", player->isLocked(card));
        row.insert("target_residue", responseResidueJson(responseResidue(m_room, player, card)));
        for (auto it = targets.begin(); it != targets.end(); ++it) row.insert(it.key(), it.value());
        if (!aiRound44RangeRequestCurrent(m_room,parentRequest)) return failure("preview evaluation changed state");
        return {{"evaluation_only", true}, {"card", row}, {"candidate", targets}, {"card_rules", rules}};
    }
    if (proposal.value("kind").toString() == "match_owner_discard_pattern") {
        const QJsonValue value = proposal.value("card_id");
        const double rawId = value.toDouble(-1);
        const QString pattern = proposal.value("pattern").toString();
        if (proposal.size() != 3 || !value.isDouble() || rawId < 0
            || rawId > std::numeric_limits<int>::max() || rawId != std::floor(rawId)
            || !proposal.value("pattern").isString() || pattern.isEmpty()
            || pattern.toUtf8().size() > 1024 || pattern.contains(QChar::Null))
            return failure("invalid owner discard pattern input");
        const int id = int(rawId);
        const auto place = m_room.getCardPlace(id);
        if (request.worldView.self.objectName != player->objectName()
            || m_room.getCardOwner(id) != player
            || (place != Player::PlaceHand && place != Player::PlaceEquip)
            || (!player->handCards().contains(id) && !player->getEquipsId().contains(id)))
            return failure("discard pattern card is outside owner hand/equipment");
        EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
        LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
        ResponseNativeScope responseScope(m_room, player, request.reason, request.pattern);
        const Card *card = Sanguosha->getCard(id);
        if (!card || card->isVirtualCard()) return failure("discard pattern physical card is unavailable");
        const bool matched = Sanguosha->matchExpPattern(pattern, player, card);
        if (!aiRound44RangeRequestCurrent(m_room,parentRequest))
            return failure("discard pattern query changed state");
        return {{"matched", matched}};
    }
    if (proposal.value("kind").toString() == "inspect_nested_cardask_string") {
        if (proposal.size() != 2 || !proposal.value("value").isString())
            return failure("invalid nested card parser input");
        EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
        LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
        ResponseNativeScope responseScope(m_room, player, request.reason, request.pattern);
        const QJsonObject result = inspectAINestedCardaskString(m_room, player, request, proposal.value("value").toString());
        if (!aiRound44RangeRequestCurrent(m_room,parentRequest))
            return failure("nested card parser changed state");
        return result;
    }
    if (proposal.value("kind").toString() == "evaluate_nengchen_preview") {
        const QJsonValue value = proposal.value("card_id");
        const double id = value.toDouble(-1);
        if (proposal.size() != 2 || !value.isDouble() || id < 0
            || id > std::numeric_limits<int>::max() || id != std::floor(id))
            return failure("invalid Nengchen material identity");
        EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
        LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
        ResponseNativeScope responseScope(m_room, player, request.reason, request.pattern);
        const QJsonObject result = evaluateAINengchenPreview(m_room, player, request, int(id));
        if (!aiRound44RangeRequestCurrent(m_room,parentRequest))
            return failure("Nengchen evaluation changed state");
        return result;
    }
    if (proposal.value("kind").toString() == "evaluate_notified_card") {
        const QJsonValue value = proposal.value("card_id");
        const double id = value.toDouble(-1);
        if (proposal.size() != 2 || !value.isDouble() || id < 0
            || id > std::numeric_limits<int>::max() || id != std::floor(id))
            return failure("invalid notified physical identity");
        QJsonObject disclosed;
        QString error;
        if (!authorizeAIOlClanNotifiedEvaluation(request, int(id), &disclosed, &error)
            && !authorizeAIRound44PoxiEvaluation(request, int(id), &disclosed))
            return failure(error);
        EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
        LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
        ResponseNativeScope responseScope(m_room, player, request.reason, request.pattern);
        const Card *card = Sanguosha->getCard(int(id));
        if (!card || card->isVirtualCard() || card->objectName() != disclosed.value("name").toString()
            || card->getClassName() != disclosed.value("class_name").toString()
            || int(card->getSuit()) != disclosed.value("suit").toInt(-1)
            || card->getNumber() != disclosed.value("number").toInt(-1))
            return failure("notified physical face changed");
        int budget = 4096;
        // The donor runs ordinary aiUseCard on the disclosed original card.
        // Its response skill pattern is not the ordinary card's pattern.
        const auto candidate = makeAICardCandidate(m_room, player, card, Card::MethodUse,
            AIRequest::Activate, QString(), budget);
        const auto rules = responseOwnerRules(m_room, player, card);
        const auto targets = responseCandidateJson(candidate);
        const bool ownMaterial = m_room.getCardOwner(int(id)) == player;
        QJsonObject row = publicDecisionCard(card, ownMaterial);
        if (!ownMaterial) { row.remove("flags"); row.remove("source_skill"); }
        row.insert("evaluation_only", true); row.insert("evaluation_owner", player->objectName());
        row.insert("evaluation_rules", rules); row.insert("target_residue", responseResidueJson(responseResidue(m_room, player, card)));
        row.insert("locked", player->isLocked(card)); row.insert("using", card->hasFlag("using"));
        row.insert("can_recast", card->canRecast()); row.insert("handling_method", int(card->getHandlingMethod()));
        row.insert("virtual_card", false); row.insert("subcards", QJsonArray());
        row.insert("legacy_string_key", card->toString());
        row.insert("legacy_sort_key", legacySortKey(card));
        for (auto it = targets.begin(); it != targets.end(); ++it) row.insert(it.key(), it.value());
        if (!aiRound44RangeRequestCurrent(m_room,parentRequest))
            return failure("notified evaluation changed state");
        return {{"evaluation_only", true}, {"card", row}, {"card_rules", rules}, {"candidate", targets}};
    }
    if (proposal.value("kind").toString() == "evaluate_skill_seed") {
        const bool costHypothesis = proposal.contains("subcards");
        const bool declarationHypothesis = proposal.contains("user_string");
        if (proposal.size() != 3 + int(costHypothesis) + int(declarationHypothesis)
            || (declarationHypothesis && !costHypothesis) || !proposal.value("scope_id").isDouble()
            || !proposal.value("class_name").isString() || request.kind != AIRequest::Activate)
            return failure("invalid strategy seed context");
        const double seedScope = proposal.value("scope_id").toDouble();
        if (seedScope < 0 || seedScope > std::numeric_limits<int>::max() || seedScope != std::floor(seedScope))
            return failure("invalid strategy seed scope");
        const int scopeId = int(seedScope);
        const auto scope = request.conversionPolicy.value("scopes").toObject().value(QString::number(scopeId)).toObject();
        if (!scope.value("skill_action").toBool() || scope.value("reason").toInt() != request.reason
            || scope.value("pattern").toString() != request.pattern) return failure("strategy seed scope was not issued");
        static const QMap<QString, QString> seedPrototypes{
            {"lol_ayjm_r", "#lol_ayjm_rCard"},
            {"lol_bjfh_e", "#lol_bjfh_eCard"},
            {"lol_bjfh_q", "#lol_bjfh_qCard"},
            {"lol_bjfh_r", "#lol_bjfh_rCard"},
            {"lol_bzll_e", "#lol_bzll_eCard"},
            {"lol_hbss_e", "#lol_hbss_eCard"},
            {"lol_hbss_q", "#lol_hbss_qCard"},
            {"lol_jwyh_e", "#lol_jwyh_eCard"},
            {"lol_jwyh_q", "#lol_jwyh_qCard"},
            {"lol_jwyh_r", "#lol_jwyh_rCard"},
            {"lol_jwyh_w", "#lol_jwyh_wCard"},
            {"lol_mzzw_q", "#lol_mzzw_qCard"},
            {"lol_mzzw_r", "#lol_mzzw_rCard"},
            {"lol_mzzw_w", "#lol_mzzw_wCard"},
            {"lol_smss_e", "#lol_smss_eCard"},
            {"lol_smss_q", "#lol_smss_qCard"},
            {"lol_smss_r", "#lol_smss_rCard"},
            {"lol_smss_w", "#lol_smss_wCard"},
            {"lol_sxls_e", "#lol_sxls_eCard"},
            {"lol_sxls_r", "#lol_sxls_rCard"},
            {"lol_sxls_w", "#lol_sxls_wCard"},
            {"lol_tqz_e", "#lol_tqz_eCard"},
            {"lol_tqz_r", "#lol_tqz_rCard"},
            {"lol_tqz_w", "#lol_tqz_wCard"},
            {"lol_xlnw_e", "#lol_xlnw_eCard"},
            {"lol_xlnw_r", "#lol_xlnw_rCard"},
            {"lol_xlnw_w", "#lol_xlnw_wCard"},
            {"PlusYiji", "#PlusYiji_Card"},
            {"fatewangzhe", "#fatewangzhe"},
            {"fateqiuzhan", "#fateqiuzhan_card"},
            {"fateshenjian", "#fateshenjian_card"},
            {"fatebumo", "#fatebumo_card"},
            {"fateyaoshu", "#fateyaoshu"},
            {"fatechuyi_vs", "#fatechuyi_card"},
            {"fatetiangong_vs", "#fatetiangong_card"},
            {"fateloli_vs", "#fateloli_card"},
            {"fatefanshi", "#fatefanshi_card"},
            {"fateheijian", "#fateheijian_card"},
            {"fatexinyin", "#fatexinyin_card"},
            {"fatemoshu_vs", "#fatemoshu_card"},
            {"fatejuntuan_vs", "#fatejuntuan_card"},
            {"fatesiji_vs", "#fatesiji_card"},
            {"fatechongshu", "#fatechongshu_card"},
            {"sy_yaohuo", "#sy_yaohuoCard"},
            {"sy_zuijiu", "#sy_zuijiuCard"},
            {"sy_zuijiuNormalSlash", "#sy_zuijiuNormalSlashCard"},
            {"sy_luansi", "#sy_luansiCard"},
            {"sy_dihui", "#sy_dihuiCard"},
            {"sy_chanxian", "#sy_chanxianCard"},
            {"sy_zhiheng", "#sy_zhiheng"},
            {"sy_zhongzhang", "#sy_zhongzhang"},
            {"sy_baofeng", "#sy_baofengCard"},
            {"sy_longnu", "#sy_longnuCard"},
            {"sy_juwu", "#sy_juwuCard"},
            {"sy_shishen", "#sy_shishenCard"},
            {"nyyuqi", "#nyyuqi"},
            {"nymiaojian", "#nymiaojian"},
            {"nyguose", "#nyguose"},
            {"nylijian", "#nylijian"},
            {"nyxuechang", "#nyxuechang"},
            {"nybeifen", "#nybeifen"},
            {"nyyuanyu", "#nyyuanyu"},
            {"nyxiyan", "#nyxiyan"},
            {"nywuniang", "#nywuniang"},
            {"ny_qiaoshi", "#ny_qiaoshi"},
            {"ny_yanyu", "#ny_yanyu"},
            {"ny_luochong", "#ny_luochong"},
            {"nyarz_caiyi", "#nyarz_caiyi"},
            {"nyarz_guangu", "#nyarz_guangu"},
            {"nyarz_zhifou", "#nyarz_zhifou"},
            {"nyarz_anzhi", "#nyarz_anzhi"},
            {"nyarz_yaoyi", "#nyarz_yaoyi"},
            {"sgkgodshajue", "#sgkgodshajueCard"},
            {"jgjiaoxie", "JGJiaoxieCard"}, {"jgyingji", "JGYingjiCard"}, {"jghanjun", "JGHanjunCard"},
            {"MR_huishi", "#MR_huishi"}, {"MR_jilve", "#MR_jilve"},
            {"MR_fanjian", "#MR_fanjian"}, {"MR_quhu", "#MR_quhu"}, {"MR_gongxin", "#MR_gongxin"},
            {"kenewgirlshixie", "#kenewgirlshixieCard"}, {"kenewgirljingmu", "#kenewgirljingmuCard"},
            {"kenewgirlbuguan", "#kenewgirlbuguanCard"}, {"kenewgirljuzhong", "#kenewgirljuzhongCard"},
            {"kenewgirlchenji", "#kenewgirlchenjiCard"}, {"kenewgirlscshixie", "#kenewgirlscshixieCard"},
            {"bifa", "BifaCard"}, {"songci", "SongciCard"},
            {"xueji", "XuejiCard"},
            {"neoluoyi", "LuoyiCard"},
            {"neofanjian", "NeoFanjianCard"},
            {"kuangxi", "KuangxiCard"}, {"yl_diting", "#yl_ditingCard"},
            {"tieba_zhili", "#tieba_zhiliCard"}, {"tiaojiaoCMT", "#tiaojiaoCMTCard"},
            {"ms_mimou", "#ms_mimou"}, {"ms_yongheng", "#ms_yonghengCard"}, {"ms_guimou", "#ms_guimou"},
            {"se_zhifu", "#se_zhifucard"}, {"se_nike", "#se_nikecard"},
            {"LuaFengxi", "#LuaFengxiCard"}, {"LuaLinlu", "#LuaLinluCard"},
            {"LuaDuanjian", "#LuaDuanjianCard"}, {"LuaHuowei", "#LuaHuoweiCard"},
            {"tongyiTQ", "#tongyiTQ"},
            {"kehuisuan", "#kehuisuanCard"},
            {"tiaojiao", "TiaojiaoCard"},
            {"sitian", "SitianCard"},
            {"lisao", "LisaoCard"},
            {"qixin", "QixinCard"},
            {"luaRsihu", "#luaRsihuCard"},
            {"luaRziao", "#luaRziao"},
            {"luaRzhangshi", "#luaRzhangshicard"},
            {"yj_xinge", "#yj_xingecard"},
            {"yj_zhengyu&", "#yj_zhengyuCard"},
            {"zd_fenyue", "#zd_fenyueCard"},
            {"jiwu", "JiwuCard"},
            {"EXCard_WLJ", "#EXCard_WLJ_SkillCARD"},
            {"juao", "JuaoCard"},
            {"houyuan", "HouyuanCard"},
            {"shouye", "ShouyeCard"},
            {"jueyan", "JueyanCard"},
            {"xiongluan", "XiongluanCard"},
            {"poxi", "PoxiCard"},
            {"huairou", "HuairouCard"},
            {"qingce", "QingceCard"},
            {"olqingce", "OLQingceCard"},
            {"sy_old_bolue", "#sy_old_bolueCard"},
            {"sy_old_yaohuo", "#sy_old_yaohuoCard"},
            {"sy_old_sanzhi", "#sy_old_sanzhiCard"},
            {"sy_old_luansi", "#sy_old_luansiCard"},
            {"sy_old_dihui", "#sy_old_dihuiCard"},
            {"sy_old_chanxian", "#sy_old_chanxianCard"},
            {"jinchoufa", "JinChoufaCard"},
            {"jinyanxi", "JinYanxiCard"},
            {"jinsanchen", "JinSanchenCard"},
            {"jinxiongzhi", "JinXiongzhiCard"},
            {"blood_hj", "#blood_hj"},
            {"zhiyuan", "ZhiyuanCard"},
            {"flood", "FloodCard"},
            {"zhongyi", "ZhongyiCard"},
            {"taichen_fight", "TaichenFightCard"},
            {"tousui", "TousuiCard"},
            {"chexuan", "ChexuanCard"},
            {"caozhao", "CaozhaoCard"},
            {"ckdianji", "#ckdianjicard"},
            {"ckluolei", "#ckluoleicard"},
            {"cksilie", "#cksiliecard"},
            {"cklongxi", "#cklongxicard"},
            {"ckweishan", "#ckweishancard"},
            {"sy_quanqing", "#sy_quanqingCard"},
            {"sy_ejue", "#sy_ejueCard"},
            {"fcmk_jlsg_meihuo", "#fcmk_jlsg_meihuoCard"},
            {"feijun", "FeijunCard"},
            {"shenshi", "ShenshiCard"},
            {"chenglve", "ChenglveCard"},
            {"zhenliang", "ZhenliangCard"},
            {"olzhenliang", "OLZhenliangCard"},
            {"zhanhuo", "ZhanhuoCard"},
            {"se_chicheng", "#se_chichengcard"},
            {"se_hongzha", "#se_hongzha"},
            {"fanqian", "#fanqian"},
            {"eryu", "#eryu"},
            {"nuequ", "#nuequ"},
            {"keshilun", "#keshilun"},
            {"jinshidu", "JinShiduCard"},
            {"jinruilve_give", "JinRuilveGiveCard"},
            {"efusha", "#efusha"},
            {"eyanshou", "#eyanshouCard"},
            {"etiaobo", "#etiaoboCard"},
            {"shenjiZHCMT", "#shenjiZHCMTCard"},
            {"zhuiluZHCMT", "#zhuiluZHCMTCard"},
            {"XiansiZHCMT", "#XiansiZHCMTCard"},
            {"XiansiZHCMTSlash", "#XiansiZHCMTSlashCard"},
            {"zhengsiZHCMT", "#zhengsiZHCMTCard"},
            {"Dragon_rende", "#Dragon_rendeCard"},
            {"Dragon_feijiang", "#Dragon_feijiangCard"},
            {"Dragon_fuyuanVS", "#Dragon_fuyuanCard"},
            {"tongling", "#tonglingCard"},
            {"Dragon_kurou", "#Dragon_kurouCard"},
            {"Dragon_jiedao", "#Dragon_jiedaoCard"},
            {"Dragon_qingnang", "#Dragon_qingnangCard"},
            {"Promote_zhiheng", "#Promote_zhihengCard"},
            {"Promote_fanjian", "#Promote_fanjianCard"},
            {"Promote_qingnang", "#Promote_qingnangCard"},

            {"LuaFanjian", "#LuaFanjianCard"}, {"LuaXinyiji", "#LuaXinyiji"}, {"LuaJuece", "#LuaJuece"},
            {"zuguangu", "#zuguanguCard"},
            {"zuyirong", "#zuyirongCard"},
            {"kezuyunshen", "#kezuyunshenCard"},
            {"kezulieshi", "#kezulieshiCard"},
            {"kezubolong", "#kezubolongCard"},
            {"zukaiji", "#zukaijiCard"},
            {"zutanfeng", "#zutanfengCard"},
            {"zulianzhuvs", "#zulianzhuCard"},
            {"kezufuxun", "#kezufuxunCard"},
            {"kezuxumin", "#kezuxuminCard"},
            {"kezumingjie", "#kezumingjieCard"},
            {"kezuqiuxin", "#kezuqiuxinCard"},
            {"biejun-give", "BiejunCard"}, {"shujian", "ShujianCard"},
            {"fujue", "FujueCard"}, {"beiyu", "BeiyuCard"},
            {"thqimei", "ThQimeiCard"}, {"gongqiao", "GongqiaoCard"},
            {"jiaozhao", "JiaozhaoCard"}, {"jiyu", "JiyuCard"}, {"duliang", "DuliangCard"},
            {"kuangbi", "KuangbiCard"}, {"jishe", "JisheDrawCard"}, {"zhige", "ZhigeCard"},
            {"zhongjian", "ZhongjianCard"}, {"olzhongjian", "OLZhongjianCard"},
            {"mobileqingxian", "MobileQingxianCard"}, {"mobilecanyun", "MobileCanyunCard"},
            {"wengua", "WenguaCard"}, {"wenguagive", "WenguagiveCard"},
            {"dingpin", "DingpinCard"}, {"shenxing", "ShenxingCard"},
            {"jiefan", "JiefanCard"}, {"anxu", "AnxuCard"},
            {"junxing", "JunxingCard"}, {"danshou", "DanshouCard"},
            {"mieji", "MiejiCard"}, {"fencheng", "FenchengCard"},
            {"mingjian", "MingjianCard"}, {"huaiyi", "HuaiyiCard"},
            {"yanzhu", "YanzhuCard"}, {"yjyanyu", "YjYanyuCard"},
            {"wurong", "WurongCard"}, {"anguo", "AnguoCard"}, {"olzhaofu", "OLzhaofuCard"},
            {"nosjujian", "NosJujianCard"},
            {"nosguhuo", "NosGuhuoCard"},
            {"jinxuanbei", "JinXuanbeiCard"}, {"jinxianwan", "JinXianwanCard"},
            {"xinzhan", "XinzhanCard"}, {"ganlu", "GanluCard"}, {"xianzhen", "XianzhenCard"},
            {"shouji", "#shoujicard"}, {"jisha", "#jishaCard"}, {"Jiuzi", "#JiuziCard"}, {"ov_muyue", "#ov_muyueCard"},
            {"nosxuanhuo", "NosXuanhuoCard"},
            {"nosrende", "NosRendeCard"},
            {"nosfencheng", "NosFenchengCard"},
            {"noskurou", "NosKurouCard"},
            {"nosfanjian", "NosFanjianCard"},
            {"jijiang", "JijiangCard"},
            {"yijue", "YijueCard"},
            {"zhiheng", "ZhihengCard"},
            {"kurou", "KurouCard"},
            {"fanjian", "FanjianCard"},
            {"jianyan", "JianyanCard"},
            {"rende", "ActiveSkillCard"},
            {"tiaoxin", "TiaoxinCard"},
            {"zhiba_pindian", "ZhibaCard"},
            {"zhijian", "ZhijianCard"},
            {"jilve", "JilveCard"},
            {"dimeng", "DimengCard"},
            {"yeyan", "GreatYeyanCard"}};
        const QString skillName = scope.value("skill_name").toString();
        const QString requestedPrototype = proposal.value("class_name").toString();
        int auditedPrototype = aiAuditedSourcePrototype(skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiAuditedConditionalSourcePrototype(request, skillName, requestedPrototype, player);
        if (auditedPrototype < 0)
            auditedPrototype = aiAuditedConstructionSourcePrototype(request, player, skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound21ReadonlyPrototype(skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound21PlanningPrototype(request, player, skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound21ChoicePrototype(request, player, skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound43InovationPrototype(request, player, skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound45InovationPrototype(request, player, skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound47SijyuPrototype(request, player, skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound51JijiaoPrototype(request, player, skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound22OlClanPrototype(request, player, skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound22OlClanActionPrototype(skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound22ChoicePrototype(request, player, skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound23ChoicePrototype(request, player, skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound24FeiDIYPrototype(request, player, skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound42PlanningPrototype(request, player, skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound24OriginalPrototype(request, player, skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound25ChoicePrototype(request, player, skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound42ChoicePrototype(request, player, skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound43ChoicePrototype(request, player, skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound44SkinPrototype(request, player, skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound45NewgeneralsPrototype(request,player,skillName,requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound46PlanningPrototype(request,player,skillName,requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound49qRenchouPrototype(request,player,skillName,requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound50PlanningPrototype(request,player,skillName,requestedPrototype);
        if (skillName == "mtjieli" && !aiRound43ChoiceDeclaration(request, player, proposal))
            return failure("Mtjieli source proxy requires zero costs and red/black declaration");
        if (auditedPrototype < 0)
            auditedPrototype = aiRound26ChoicePrototype(request, player, skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound27ChoicePrototype(request, player, skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound25ResponsePrototype(request, player, skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound42ResponsePrototype(request, player, skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound44LeiPrototype(request, player, skillName, requestedPrototype);
        if (skillName == "limu" && costHypothesis
            && proposal.value("subcards").toArray().size() != 1)
            return failure("Limu source readonly SkillCard requires exactly one material");
        if (auditedPrototype < 0)
            auditedPrototype = aiRound25OriginalPrototype(request, player, skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound26OriginalPrototype(request, player, skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound27OriginalPrototype(request, player, skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound36KurouPrototype(request, player, skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound29ExtraPrototype(request, player, skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound26DelegatePrototype(request, player, skillName, requestedPrototype);
        if (auditedPrototype < 0)
            auditedPrototype = aiRound22OlClanObservationPrototype(request, player, skillName, requestedPrototype);
        if (skillName == "renchou" && (declarationHypothesis
            || (costHypothesis && (!proposal.value("subcards").isArray() || !proposal.value("subcards").toArray().isEmpty()))))
            return failure("Renchou source SkillCard requires zero costs and no declaration");
        if (costHypothesis && !seedPrototypes.contains(skillName) && auditedPrototype < 0)
            return failure("cost seed is outside audited source catalog");
        if (declarationHypothesis && skillName != "caozhao" && !(auditedPrototype >= 0 && (auditedPrototype & 2)))
            return failure("source seed has no declaration");
        // Fixed source fill is @ZunweiCard=. and native ZeroCardViewAsSkill
        // accepts no materials. Do not extend even the readonly source shape.
        if (skillName == "zunwei" && (declarationHypothesis
            || (costHypothesis && (!proposal.value("subcards").isArray()
                || !proposal.value("subcards").toArray().isEmpty()))))
            return failure("Zunwei source seed requires zero materials and no declaration");
        const QString prototype = auditedPrototype >= 0 ? requestedPrototype : seedPrototypes.value(skillName);
        if (prototype.isEmpty() || prototype != proposal.value("class_name").toString())
            return failure("strategy prototype does not belong to issued skill scope");
        const QJsonObject identity = scope.value("identity").toObject();
        const auto activation = responseRef(identity, "activation"), source = responseRef(identity, "source");
        EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
        LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
        ResponseNativeScope responseScope(m_room, player, request.reason, request.pattern);
        bool enabled = false;
        for (const SkillInstance &instance : player->getSkillInstances()) {
            if (activation.ownerObjectName != player->objectName() || instance.key() != activation.key
                || ([&] {
                    std::unique_ptr<AIRound56SkillValidityScope> validitySource;
                    auto *gameLua=m_room.roomRuntime()->lua().rawState();
                    if(parentRequest.kind==AIRequest::Activate&&request.kind==AIRequest::Activate
                        &&request.reason==CardUseStruct::CARD_USE_REASON_PLAY
                        &&!inAIRound56ProjectionScope(m_room,gameLua))
                        validitySource.reset(new AIRound56SkillValidityScope(m_room,player,gameLua));
                    return player->isSkillInvalid(instance.skillName,instance.instanceID);
                }())) continue;
            AiSkillActionContext current;
            enabled = buildSkillActionContext(player, instance, request.reason, request.pattern, current)
                && current.sourceRef == source;
            break;
        }
        if (!enabled) return failure("strategy seed activation changed");
        std::unique_ptr<AIRound56ZunweiSeedScope> zunweiSource;
        if(parentRequest.kind==AIRequest::Activate&&request.kind==AIRequest::Activate
            &&request.reason==CardUseStruct::CARD_USE_REASON_PLAY&&request.pattern.isEmpty()
            &&skillName=="zunwei"&&prototype=="ZunweiCard"
            &&!inAIRound56ProjectionScope(m_room,m_room.roomRuntime()->lua().rawState()))
            zunweiSource.reset(new AIRound56ZunweiSeedScope(m_room,player,m_room.roomRuntime()->lua().rawState()));
        std::unique_ptr<SkillCard> card(prototype.startsWith('#')
            ? LuaSkillCard::Parse(prototype + ":.:") : Sanguosha->cloneSkillCard(prototype));
        if (!card || !card->getSubcards().isEmpty()) return failure("legacy strategy prototype unavailable");
        QJsonArray materialIds;
        if (costHypothesis) {
            if (!proposal.value("subcards").isArray() || proposal.value("subcards").toArray().size() > 128)
                return failure("readonly source materials are malformed");
            const bool tyshouliPublicSeed = aiRound40TyshouliSeed(request, m_room, player, skillName, prototype, proposal);
            const QJsonArray values = proposal.value("subcards").toArray();
            QSet<int> seen;
            for (const QJsonValue &value : values) {
                const double raw = value.toDouble(-1);
                if (!value.isDouble() || raw < 0 || raw > std::numeric_limits<int>::max() || raw != std::floor(raw))
                    return failure("readonly source material ID is malformed");
                const int id = int(raw);
                bool disclosed = false;
                for (const AICardView &known : request.worldView.handCards) if (known.effectiveId == id) disclosed = true;
                for (const AICardView &known : request.worldView.self.equips) if (known.effectiveId == id) disclosed = true;
                for (const AICardPileView &pile : request.worldView.self.piles)
                    for (const AICardView &known : pile.cards) if (known.effectiveId == id) disclosed = true;
                if (seen.contains(id) || (!tyshouliPublicSeed && (!disclosed || m_room.getCardOwner(id) != player)) || !Sanguosha->getCard(id))
                    return failure("readonly source material is not an owned disclosed card");
                seen.insert(id); card->addSubcard(id); materialIds.append(id);
            }
            if (declarationHypothesis) {
                if (!proposal.value("user_string").isString()) return failure("readonly declaration is malformed");
                card->setUserString(proposal.value("user_string").toString());
            }
        }
        if (proposal.value("class_name").toString() == "ActiveSkillCard")
            card->setSkillName(scope.value("skill_name").toString());
        AIRound47XufaSourceCard xufaSource(m_room,parentRequest,player,card.get(),skillName);
        QJsonObject row = publicDecisionCard(card.get(), true);
        row.insert("evaluation_owner", player->objectName());
        row.insert("evaluation_rules", responseOwnerRules(m_room, player, card.get()));
        row.insert("target_residue", responseResidueJson(responseResidue(m_room, player, card.get())));
        row.insert("evaluation_only", true); row.insert("strategy_seed", true); row.insert("virtual_card", true);
        if (skillName == "mtjieli") row.insert("user_string", card->getUserString());
        row.insert("id", -1000000-scopeId); row.insert("effective_id", -1000000-scopeId);
        row.insert("scope_id", scopeId); row.insert("subcards", materialIds);
        if (costHypothesis && !materialIds.isEmpty()) row.insert("effective_id", materialIds.first());
        row.insert("available", card->isAvailable(player)); row.insert("limited", player->isCardLimited(card.get(), Card::MethodUse));
        row.insert("locked", player->isLocked(card.get())); row.insert("jilei", player->isJilei(card.get()));
        row.insert("using", false); row.insert("can_recast", card->canRecast());
        row.insert("handling_method", int(card->getHandlingMethod()));
        row.insert("legacy_string_key", card->toString()); row.insert("legacy_sort_key", legacySortKey(card.get()));
        for (auto it = identity.begin(); it != identity.end(); ++it) row.insert(it.key(), it.value());
        QJsonObject result{{"card", row}, {"identity", identity}, {"scope_id", scopeId}};
        if (costHypothesis) {
            int budget = 4096;
            auto candidate = makeAICardCandidate(m_room, player, card.get(),
                Card::MethodUse, AIRequest::Activate, QString(), budget);
            candidate.cardId = -1000000-scopeId;
            result.insert("candidate", responseCandidateJson(candidate));
        }
        zunweiSource.reset(); // All native evaluation ends before DTO publication.
        if (!aiRound44RangeRequestCurrent(m_room,parentRequest)) return failure("strategy prototype changed state");
        if (nestedPlay) {
            const auto recipeKey = m_room.roomRuntime()->ai().issueReadonlyCard(parentRequest,
                player, std::move(card), skillName);
            if (recipeKey.isEmpty()) return failure("nested strategy prototype has no active parent lifetime");
            row.insert("readonly_recipe_key", recipeKey);
            result.insert("card", row);
        }
        return result;
    }
    if (proposal.value("kind").toString() == "evaluate_zenhui_card") {
        if (proposal.size() != 1 || request.kind != AIRequest::PlayerChosen
            || request.choiceOptions.reason != "zenhui" || !player->hasSkill("zenhui"))
            return failure("Zenhui evaluation prompt is not active");
        const QVariant value = player->getTag("zenhui");
        if (!value.canConvert<CardUseStruct>()) return failure("Zenhui use is absent");
        const CardUseStruct use = value.value<CardUseStruct>();
        if (!use.card || use.from != player || use.to.isEmpty()) return failure("Zenhui use is not the viewer's declaration");
        EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
        LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
        ResponseNativeScope responseScope(m_room, player, request.reason, request.pattern);
        std::unique_ptr<Card> card(Sanguosha->cloneCard(use.card));
        if (!card) return failure("Zenhui declaration cannot be evaluated");
        AITianyiQueryScope hypothetical(player, card.get(), 99, false);
        int budget = 4096;
        auto candidate = makeAICardCandidate(m_room, player, card.get(), Card::MethodUse, AIRequest::Activate, {}, budget);
        if (candidate.extraTargetKnown) candidate.extraTargetCount -= 99;
        for (auto it = candidate.extraTargetByPlayer.begin(); it != candidate.extraTargetByPlayer.end(); ++it) it.value() -= 99;
        QJsonObject row = publicDecisionCard(card.get(), true);
        row.insert("evaluation_only", true); row.insert("virtual_card", card->isVirtualCard());
        row.insert("effective_id", card->getEffectiveId()); row.insert("subcards", responseIds(card->getSubcards()));
        row.insert("evaluation_owner", player->objectName());
        auto rules = responseOwnerRules(m_room,player,card.get());
        rules.insert("target_mod_extra", rules.value("target_mod_extra").toInt() - 99);
        row.insert("evaluation_rules", rules);
        row.insert("target_residue", responseResidueJson(responseResidue(m_room,player,card.get())));
        row.insert("locked", player->isLocked(card.get())); row.insert("using",card->hasFlag("using"));
        const auto targets = responseCandidateJson(candidate);
        for (auto it = targets.begin(); it != targets.end(); ++it) row.insert(it.key(),it.value());
        if (!aiRound44RangeRequestCurrent(m_room,parentRequest)) return failure("Zenhui evaluation changed state");
        return {{"card",row},{"candidate",targets}};
    }
    if (proposal.value("kind").toString() == "card_target_filter") {
        const bool recipeQuery = proposal.contains("recipe_key");
        const bool prohibitQuery = proposal.value("operation")==QJsonValue("is_prohibited");
        const bool slashQuery=proposal.value("operation")==QJsonValue("can_slash");
        bool round50Recipe=false;QString round50Source;
        if (proposal.size() != 4+int(prohibitQuery || slashQuery) || (recipeQuery ? !proposal.value("recipe_key").isString()
                : !proposal.value("card_id").isDouble()) || !proposal.value("prefix").isArray()
            || !proposal.value("target").isString()) return failure("invalid target filter query");
        const double number = proposal.value("card_id").toDouble();
        if (!recipeQuery && (number != std::floor(number) || number < -128000 || number > std::numeric_limits<int>::max()))
            return failure("invalid target filter identity");
        const int id = int(number);
        EngineRuntimeContextScope contextScope(*Sanguosha,&m_room);
        LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(),false);
        ResponseNativeScope responseScope(m_room,player,request.reason,request.pattern);
        std::unique_ptr<Card> ownedCard;
        const Card *card = nullptr;
        if (recipeQuery) {
            const QString key = proposal.value("recipe_key").toString();
            auto &runtime = m_room.roomRuntime()->ai();
            card = runtime.readonlyCard(parentRequest, player, key);
            if (!card) return failure("readonly target filter recipe is outside the current viewer/request/lifetime");
            // Only the already-issued unskilled printed prototype domain.
            // Specialized extra-target recipes retain their sealed query route.
            const QString source = runtime.readonlyCardSource(parentRequest, player, key);
            round50Recipe=aiRound50PlanningRecipe(request,m_room,player,source,card);
            if(round50Recipe)round50Source=source;
            if (!round50Recipe && ((!source.isEmpty() && source != QStringLiteral("heg_family_printed"))
                || !card->getSkillName(false).isEmpty() || !card->getSubcards().isEmpty()))
                return failure("readonly target filter recipe is not an unskilled printed prototype");
        } else if (id >= 0) {
            bool issued = false;
            for (const auto &candidate : request.cardCandidates) if (candidate.cardId == id) issued = true;
            // Exact Caokong actual-card observation is readonly, not an issued payment.
            const Card *caokong = aiRound26PlanningEvaluation(request, m_room, player, "luacaokong_context");
            if (caokong && caokong->getEffectiveId() == id) card = caokong;
            else {
                if (!issued) return failure("target filter card was not issued");
                ownedCard.reset(Sanguosha->cloneCard(Sanguosha->getCard(id)));
                card = ownedCard.get();
            }
        } else {
            const auto *ticket = responseTicket(request,-id);
            if (!ticket || ticket->activationRef.ownerObjectName != player->objectName() || ticket->typeId < 1)
                return failure("target filter conversion was not issued");
            ownedCard.reset(Sanguosha->cloneCard(ticket->name,Card::Suit(ticket->suit),ticket->number));
            if (ownedCard) { ownedCard->addSubcards(ticket->subcardIds); ownedCard->setSkillName(ticket->skillName); }
            card = ownedCard.get();
        }
        if (!card) return failure("target filter card is unavailable");
        const auto prefix = proposal.value("prefix").toArray();
        if (prefix.size() > 128) return failure("target prefix too long");
        QList<const Player *> targets;
        for (const auto &name : prefix) {
            const auto *target = name.isString() ? m_room.findPlayerByObjectName(name.toString()) : nullptr;
            if (!target || !target->isAlive()) return failure("target prefix references absent player");
            targets << target;
        }
        const auto *target = m_room.findPlayerByObjectName(proposal.value("target").toString());
        if (!target || !target->isAlive()) return failure("target filter references absent player");
        if (prohibitQuery && round50Source!="jxyangjie") return failure("prohibition prefix operation requires exact Jxyangjie recipe");
        if (slashQuery && (round50Source!="gfengying" || !targets.isEmpty())) return failure("canSlash operation requires exact Gfengying recipe and empty prefix");
        if(round50Source=="gfengying"&&!slashQuery)return failure("Gfengying recipe only permits its source canSlash call");
        const bool accepted = slashQuery ? player->canSlash(target,card,false,0) : prohibitQuery ? player->isProhibited(target,card,targets) : card->targetFilter(targets,target,player);
        if (!aiRound44RangeRequestCurrent(m_room,parentRequest)) return failure("target filter changed state");
        return {{"accepted",accepted}};
    }
    if (proposal.value("kind").toString() == "qiuxin_target_filter") {
        if (proposal.size() != 4 || !proposal.value("recipe_key").isString()
            || !proposal.value("prefix").isArray() || !proposal.value("target").isString())
            return failure("invalid Qiuxin prefix query");
        const auto key = proposal.value("recipe_key").toString();
        auto &runtime = m_room.roomRuntime()->ai();
        const Card *card = runtime.readonlyCard(parentRequest, player, key);
        const QString sourceKey = runtime.readonlyCardSource(parentRequest, player, key);
        EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
        LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
        ResponseNativeScope responseScope(m_room, player, request.reason, request.pattern);
        if (sourceKey == "newgenerals.mouxieji")
            return aiRound42PlanningTargetFilter(m_room,player,request,card,sourceKey,
                {{"prefix",proposal.value("prefix")},{"target",proposal.value("target")}});
        if (sourceKey=="newgenerals_context:moujiang")
            return aiRound45MoujiangTargetFilter(m_room,player,request,card,{{"prefix",proposal.value("prefix")},{"target",proposal.value("target")}});
        if (sourceKey.startsWith("fcDIY_context:"))
            return aiRound28PlanningTargetFilter(m_room,player,request,card,sourceKey,
                {{"prefix",proposal.value("prefix")},{"target",proposal.value("target")}});
        if (sourceKey == "feihujue" || sourceKey == "feilangduo")
            return aiRound24FeiDIYTargetFilter(m_room, player, request, card, sourceKey,
                {{"prefix", proposal.value("prefix")}, {"target", proposal.value("target")}});
        return aiRound23QiuxinTargetFilter(m_room, player, request, card, sourceKey,
            {{"prefix", proposal.value("prefix")}, {"target", proposal.value("target")}});
    }
    if (proposal.value("kind").toString() == "evaluate_owner_range_card") {
        bool flagValue = false;
        if (!AIOwnerRangeQueryScope::lookup(player, "InfinityAttackRange", flagValue)
            && !AIOwnerRangeQueryScope::lookup(player, "slashNoDistanceLimit", flagValue)
            && AIOwnerRangeQueryScope::sourceKey(player).isEmpty()
            && !AIRound25OwnerDistanceScope::active(player)
            && !aiRound44RangeContinuation(m_room,parentRequest))
            return failure("range card query is outside authenticated owner hypothesis");
        if (proposal.size() != 2 || !proposal.value("spec").isObject())
            return failure("invalid range card query");
        // Post-clear continuation queries use current real flags and bypass
        // any old TargetModMemoScope entry; this scope changes no flag value.
        std::unique_ptr<AIOwnerRangeQueryScope> continuationScope;
        if(!AIOwnerRangeQueryScope::isActive() && aiRound44RangeContinuation(m_room,parentRequest))
            continuationScope.reset(new AIOwnerRangeQueryScope(player,AIOwnerRangeQueryScope::InfinityAttackRange,true,QString(),true));
        const auto spec = proposal.value("spec").toObject();
        if(spec.size()==1 && spec.value("session_current")==true) {
            // A cheap active-session attestation, not a new snapshot, flag
            // oracle or authority. Retained Lua cards cannot replay a cache.
            return QJsonObject{{"kind","owner_range_session"},{"viewer",parentRequest.viewerObjectName},
                {"decision_id",parentRequest.getDecisionId()},{"state_revision",parentRequest.getStateRevision()}};
        }
        if(spec.size()==1 && spec.value("owner_state")==true) {
            const auto source=AIOwnerRangeQueryScope::sourceKey(player);
            return projectAIStrategyOwnerRange(m_room,player,QString(),source);
        }
        if(spec.contains("card_flag"))return writeAIRound44RangeQinggang(request,m_room,player,spec);
        const auto integer = [](const QJsonValue &v, int &n) {
            const double d = v.toDouble();
            if (!v.isDouble() || !std::isfinite(d) || d != std::floor(d)
                || d < std::numeric_limits<int>::min() || d > std::numeric_limits<int>::max()) return false;
            n = int(d); return true;
        };
        const auto disclosedOwner = [&](int id) {
            if (id < 0 || m_room.getCardOwner(id) != player) return false;
            for (const auto &c : request.worldView.handCards) if (c.effectiveId == id) return true;
            for (const auto &c : request.worldView.self.equips) if (c.effectiveId == id) return true;
            for (const auto &pile : request.worldView.self.piles)
                for (const auto &c : pile.cards) if (c.effectiveId == id) return true;
            return false;
        };
        static const QSet<QString> queryFields{"target_prefix", "target", "can_slash_target", "distance_limit", "range_fix"};
        static const QSet<QString> virtualFields{"name", "suit", "number", "rawskill", "subcards", "can_recast", "flags"};
        for (auto it = spec.begin(); it != spec.end(); ++it)
            if (!queryFields.contains(it.key()) && !(spec.contains("recipe_key") ? it.key() == "recipe_key"
                    : spec.contains("id") ? it.key() == "id" : virtualFields.contains(it.key())))
                return failure("unaudited range card field");
        EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
        LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
        ResponseNativeScope responseScope(m_room, player, request.reason, request.pattern);
        std::unique_ptr<Card> clone;
        const Card *card = nullptr;
        bool qiuxin = false;
        QString extraSourceKey;
        if (spec.contains("recipe_key")) {
            if (!spec.value("recipe_key").isString()) return failure("range recipe key is malformed");
            card = m_room.roomRuntime()->ai().readonlyCard(parentRequest, player, spec.value("recipe_key").toString());
            if (!card) return failure("range recipe is outside the current native producer lifetime");
            const QString sourceKey = m_room.roomRuntime()->ai().readonlyCardSource(parentRequest, player, spec.value("recipe_key").toString());
            if (sourceKey == "kezuqiuxin") {
                QString error;
                if (!aiRound23QiuxinSource(request, player, card, sourceKey, &error)) return failure(error);
                qiuxin = true; extraSourceKey = sourceKey;
            } else if (sourceKey == "feihujue" || sourceKey == "feilangduo") {
                QString error;
                if (!aiRound24FeiDIYSource(request, player, card, sourceKey, &error)) return failure(error);
                qiuxin = true; extraSourceKey = sourceKey;
            } else if (sourceKey == "newgenerals.mouxieji") {
                return failure("Mouxieji extra2 profile cannot be reused in an unrelated owner-range scope");
            } else if (card->getSkillName(false) == "_kezuqiuxin") {
                return failure("Qiuxin printing does not identify its native producer");
            }
        } else if (spec.contains("id")) {
            int id;
            if (!integer(spec.value("id"), id) || !disclosedOwner(id))
                return failure("range physical card is not disclosed owner material");
            card = Sanguosha->getCard(id);
        } else {
            int suit, number;
            if (!spec.value("name").isString() || !spec.value("rawskill").isString()
                || spec.value("name").toString().size() > 128 || spec.value("rawskill").toString().size() > 128
                || !integer(spec.value("suit"), suit) || suit < int(Card::SuitToBeDecided) || suit > int(Card::NoSuit)
                || !integer(spec.value("number"), number) || number < -1 || number > 13
                || !spec.value("subcards").isArray() || !spec.value("flags").isArray()
                || !spec.value("can_recast").isBool()) return failure("invalid range virtual card identity");
            QList<int> costs;
            for (const auto &v : spec.value("subcards").toArray()) {
                int id;
                if (!integer(v, id) || costs.contains(id) || !disclosedOwner(id))
                    return failure("range virtual material is outside viewer disclosure");
                costs.append(id);
            }
            // Only an unchanged printed prototype already issued to this viewer
            // can be reconstructed without native card tags/change-card identity.
            // Other virtual products need a separate issued recipe contract.
            const auto issued = request.worldView.evaluationCards.value(spec.value("name").toString()).toObject();
            if (issued.isEmpty() || !costs.isEmpty() || !spec.value("rawskill").toString().isEmpty()
                || !spec.value("flags").toArray().isEmpty()
                || issued.value("suit") != suit || issued.value("number") != number
                || issued.value("can_recast") != spec.value("can_recast"))
                return failure("range virtual product lacks an unchanged issued printed prototype");
            clone.reset(Sanguosha->cloneCard(spec.value("name").toString(), Card::Suit(suit), number));
            if(clone)recordAIRound56ProjectionSourceCard(m_room,clone.get(),spec.value("name").toString());
            if (!clone || clone->getTypeId() == Card::TypeSkill) return failure("range query requires ordinary card");
            clone->setSkillName(spec.value("rawskill").toString());
            clone->addSubcards(costs); clone->setCanRecast(spec.value("can_recast").toBool());
            for (const auto &v : spec.value("flags").toArray()) {
                if (!v.isString() || v.toString().isEmpty() || v.toString().size() > 128 || v.toString().startsWith('-'))
                    return failure("invalid range clone flag");
                clone->setFlags(v.toString());
            }
            card = clone.get();
        }
        if (!card) return failure("range query card is unavailable");
        const QString rangeSource=AIOwnerRangeQueryScope::sourceKey(player);
        const int rangeExtra=aiRound44RangeExtra(rangeSource);
        if(qiuxin && rangeExtra!=0)return failure("independent source target overlays cannot be silently combined");
        const int appliedExtra=qiuxin?99:rangeExtra;
        const bool finiteQueries=qiuxin||rangeExtra!=0;
        std::unique_ptr<AITianyiQueryScope> qiuxinExtra;
        if (appliedExtra) qiuxinExtra.reset(new AITianyiQueryScope(player, card, appliedExtra, false));
        if (spec.contains("target") || spec.contains("target_prefix")) {
            if (!spec.value("target").isString() || !spec.value("target_prefix").isArray())
                return failure("invalid range target prefix");
            QList<const Player *> prefix;
            for (const auto &v : spec.value("target_prefix").toArray()) {
                const auto *p = v.isString() ? m_room.findPlayerByObjectName(v.toString()) : nullptr;
                if (!p || !p->isAlive() || prefix.contains(p)) return failure("invalid range prefix player");
                prefix.append(p);
            }
            const auto *target = m_room.findPlayerByObjectName(spec.value("target").toString());
            if (!target || !target->isAlive()) return failure("range target is absent");
            const bool accepted = card->targetFilter(prefix, target, player);
            if (!aiRound44RangeRequestCurrent(m_room,parentRequest)) return failure("range query changed state");
            return {{"target_filter", accepted}};
        }
        if (spec.contains("can_slash_target")) {
            int fix;
            const auto *target = m_room.findPlayerByObjectName(spec.value("can_slash_target").toString());
            if (!target || !target->isAlive() || !spec.value("distance_limit").isBool()
                || !integer(spec.value("range_fix"), fix) || !card->isKindOf("Slash"))
                return failure("invalid range Slash query");
            const bool accepted = player->canSlash(target, card, spec.value("distance_limit").toBool(), fix);
            if (!aiRound44RangeRequestCurrent(m_room,parentRequest)) return failure("range query changed state");
            return {{"can_slash", accepted}};
        }
        int budget = 4096;
        bool sourceQueriesComplete = false;
        auto candidate = makeAICardCandidate(m_room, player, card, Card::MethodUse,
            AIRequest::Activate, QString(), budget, finiteQueries, &sourceQueriesComplete);
        if (finiteQueries && !sourceQueriesComplete) return failure("Qiuxin finite range queries are incomplete");
        if (finiteQueries) {
            if (candidate.extraTargetKnown) candidate.extraTargetCount -= appliedExtra;
            for (auto it = candidate.extraTargetByPlayer.begin(); it != candidate.extraTargetByPlayer.end(); ++it) it.value() -= appliedExtra;
        }
        auto targets = responseCandidateJson(candidate);
        if (qiuxin) targets.insert("readonly_source_queries", QJsonObject{{"source_key", extraSourceKey}, {"extra_target", 99}});
        if(rangeExtra)targets.insert("readonly_source_queries",QJsonObject{{"source_key","owner_range:"+rangeSource},{"extra_target",rangeExtra}});
        auto row = publicDecisionCard(card, true);
        row.insert("subcards", responseIds(card->getSubcards()));
        row.insert("virtual_card", card->isVirtualCard()); row.insert("can_recast", card->canRecast());
        row.insert("evaluation_owner", player->objectName());
        auto rules = responseOwnerRules(m_room, player, card);
        if (finiteQueries) rules.insert("target_mod_extra", rules.value("target_mod_extra").toInt() - appliedExtra);
        row.insert("evaluation_rules", rules);
        row.insert("locked", player->isLocked(card)); row.insert("using", card->hasFlag("using"));
        row.insert("jilei", player->isJilei(card));
        row.insert("target_residue", responseResidueJson(responseResidue(m_room, player, card)));
        QJsonObject slashMap;
        if (card->isKindOf("Slash")) {
            QSet<int> fixes{0, 1};
            if (const Weapon *weapon = qobject_cast<const Weapon *>(player->getWeapon()))
                fixes.insert(weapon->getRange(player) - player->getAttackRange(false));
            for (const auto *target : m_room.getAlivePlayers()) {
                QJsonObject modes;
                for (bool distance : {false, true}) {
                    QJsonObject values;
                    for (int fix : fixes) values.insert(QString::number(fix), player->canSlash(target, card, distance, fix));
                    modes.insert(distance ? "true" : "false", values);
                }
                slashMap.insert(target->objectName(), modes);
            }
        }
        if (!aiRound44RangeRequestCurrent(m_room,parentRequest)) return failure("range query changed state");
        return {{"card", row}, {"candidate", targets}, {"can_slash_map", slashMap}};
    }
    if (proposal.value("kind").toString() == "evaluate_tianyi_slash") {
        if (proposal.size() != 2 || !proposal.value("card_id").isDouble() || !player->hasSkill("tianyi")
            || request.kind != AIRequest::Activate) return failure("Tianyi evaluation scope unavailable");
        const double number = proposal.value("card_id").toDouble();
        if (number != std::floor(number) || number < -128000 || number > std::numeric_limits<int>::max())
            return failure("invalid Tianyi evaluation identity");
        const int id = int(number);
        std::unique_ptr<Card> card;
        if (id >= 0) {
            bool visible = false;
            for (const AICardView &known : request.worldView.handCards) if (known.effectiveId == id) visible = true;
            if (!visible || !player->handCards().contains(id)) return failure("Tianyi material is not viewer hand");
            card.reset(Sanguosha->cloneCard(Sanguosha->getCard(id)));
        } else {
            const auto *ticket = responseTicket(request, -id);
            if (!ticket || ticket->activationRef.ownerObjectName != player->objectName())
                return failure("Tianyi conversion was not issued to viewer");
            card.reset(Sanguosha->cloneCard(ticket->name, Card::Suit(ticket->suit), ticket->number));
            if (card) { card->addSubcards(ticket->subcardIds); card->setSkillName(ticket->skillName); }
        }
        if (!card || !card->isKindOf("Slash")) return failure("Tianyi evaluation requires Slash");
        EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
        LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
        ResponseNativeScope responseScope(m_room, player, request.reason, request.pattern);
        AITianyiQueryScope hypothetical(player, card.get());
        int budget = 4096;
        AICardCandidateView candidate = makeAICardCandidate(m_room, player, card.get(),
            Card::MethodUse, AIRequest::Activate, QString(), budget);
        // Source dummy.extra_target adds one in Lua. Keep the base count in the
        // descriptor while the native prefix/combinations include that extra slot.
        if (candidate.extraTargetKnown) --candidate.extraTargetCount;
        for (auto it = candidate.extraTargetByPlayer.begin(); it != candidate.extraTargetByPlayer.end(); ++it) --it.value();
        QJsonObject row = publicDecisionCard(card.get(), true);
        row.insert("evaluation_only", true); row.insert("virtual_card", card->isVirtualCard());
        row.insert("effective_id", id); row.insert("subcards", responseIds(card->getSubcards()));
        row.insert("can_recast", card->canRecast()); row.insert("locked", player->isLocked(card.get()));
        row.insert("using", card->hasFlag("using")); row.insert("target_fixed", card->targetFixed());
        row.insert("handling_method", int(card->getHandlingMethod()));
        row.insert("target_residue", responseResidueJson(responseResidue(m_room, player, card.get())));
        auto rules = responseOwnerRules(m_room,player,card.get());
        rules.insert("target_mod_extra", rules.value("target_mod_extra").toInt() - 1);
        row.insert("evaluation_rules", rules);
        row.insert("evaluation_owner", player->objectName());
        const auto targetData = responseCandidateJson(candidate);
        for (auto it = targetData.begin(); it != targetData.end(); ++it) row.insert(it.key(), it.value());
        if (!aiRound44RangeRequestCurrent(m_room,parentRequest)) return failure("Tianyi evaluation changed state");
        return {{"card", row}, {"candidate", targetData}};
    }
    if (proposal.value("kind").toString() == "evaluate_tongyi_policy") {
        const double scopeNumber = proposal.value("scope_id").toDouble(-1);
        if (proposal.size() != 4 || !proposal.value("scope_id").isDouble()
            || scopeNumber < 0 || scopeNumber > std::numeric_limits<int>::max() || scopeNumber != std::floor(scopeNumber)
            || !proposal.value("class_name").isString() || !proposal.value("declaration").isString()
            || (request.kind != AIRequest::RespondCard && request.kind != AIRequest::UseCard))
            return failure("invalid Tongyi readonly policy request");
        const auto scope = request.conversionPolicy.value("scopes").toObject().value(QString::number(int(scopeNumber))).toObject();
        const QString declaration = proposal.value("declaration").toString();
        if (scope.value("skill_name") != "tongyiTQ" || scope.value("skill_action").toBool()
            || scope.value("class_name") != proposal.value("class_name") || declaration.isEmpty()
            || scope.value("card_name") != declaration || !player->hasSkill("tongyiTQ"))
            return failure("Tongyi readonly policy has no matching issued owner scope");
        EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
        LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
        ResponseNativeScope responseScope(m_room, player, request.reason, request.pattern);
        std::unique_ptr<SkillCard> card(LuaSkillCard::Parse("#tongyiTQ:.:") );
        if (!card) return failure("Tongyi readonly prototype is unavailable");
        card->setUserString(declaration);
        if (!card->getSubcards().isEmpty()) return failure("Tongyi readonly prototype unexpectedly has costs");
        int budget = 4096;
        const auto candidate = makeAICardCandidate(m_room, player, card.get(), request.handlingMethod,
            request.kind, request.pattern, budget);
        const auto rules = responseOwnerRules(m_room, player, card.get());
        const auto targetData = responseCandidateJson(candidate);
        QJsonObject row = publicDecisionCard(card.get(), true);
        row.insert("evaluation_only", true); row.insert("evaluation_owner", player->objectName());
        row.insert("virtual_card", card->isVirtualCard());
        row.insert("legacy_selector", "#tongyiTQ"); row.insert("legacy_string_key", card->toString());
        row.insert("legacy_sort_key", legacySortKey(card.get())); row.insert("subcards", QJsonArray());
        row.insert("using", card->hasFlag("using")); row.insert("locked", player->isLocked(card.get()));
        row.insert("will_throw", card->willThrow()); row.insert("can_recast", card->canRecast());
        row.insert("handling_method", int(card->getHandlingMethod())); row.insert("target_fixed", card->targetFixed());
        row.insert("matches_response_pattern", candidate.available); row.insert("evaluation_rules", rules);
        row.insert("target_residue", responseResidueJson(responseResidue(m_room, player, card.get())));
        for (auto it = targetData.begin(); it != targetData.end(); ++it) row.insert(it.key(), it.value());
        // smart-ai3859 checks the class dummy's limit/pattern before selecting
        // the unchanged SkillCard. This is policy evidence, not availability.
        QJsonObject classPlanning{{"exists", false}};
        std::unique_ptr<Card> proxy(Sanguosha->cloneCard(proposal.value("class_name").toString(), Card::NoSuit, 0));
        if (proxy) {
            proxy->setSkillName(card->getSkillName());
            if (!card->willThrow()) proxy->addSubcards(card->getSubcards());
            const auto proxyCandidate = makeAICardCandidate(m_room, player, proxy.get(), request.handlingMethod,
                request.kind, request.pattern, budget);
            const auto proxyRules = responseOwnerRules(m_room, player, proxy.get());
            const auto proxyTargets = responseCandidateJson(proxyCandidate);
            auto face = publicDecisionCard(proxy.get(), true);
            face.insert("evaluation_only", true); face.insert("evaluation_owner", player->objectName());
            face.insert("virtual_card", proxy->isVirtualCard());
            face.insert("evaluation_rules", proxyRules); face.insert("can_recast", proxy->canRecast());
            face.insert("handling_method", int(proxy->getHandlingMethod()));
            face.insert("target_residue", responseResidueJson(responseResidue(m_room, player, proxy.get())));
            for (auto it = proxyTargets.begin(); it != proxyTargets.end(); ++it) face.insert(it.key(), it.value());
            classPlanning = {{"exists", true}, {"card", face}, {"candidate", proxyTargets},
                {"source_pattern_match", Sanguosha->matchPattern(request.pattern, player, proxy.get())},
                {"source_limited", player->isCardLimited(proxy.get(), request.handlingMethod)}};
        }
        row.insert("response_class_planning", classPlanning);
        if (!aiRound44RangeRequestCurrent(m_room,parentRequest)) return failure("Tongyi readonly query changed state");
        return {{"kind", "tongyi_policy_evaluation"}, {"evaluation_only", true},
            {"scope_id", int(scopeNumber)}, {"class_name", proposal.value("class_name")},
            {"declaration", declaration}, {"card", row}, {"card_rules", rules}, {"candidate", targetData}};
    }
    if (proposal.value("kind").toString() == "evaluate_owner_view_as") {
        const QJsonObject spec = proposal.value("spec").toObject();
        static const QMap<QString, QStringList> outputs{
            {"melee_peach", {"slash"}}, {"noslonghun", {"fire_slash"}}, {"jieyou", {"analeptic"}}, {"jiuxian", {"analeptic"}}, {"GodQuxiang", {"savage_assault"}}, {"GodGongshen", {"archery_attack"}}, {"jianhun", {"slash"}}, {"qingguo", {"jink"}}, {"guihao", {"analeptic"}}, {"LuaYanjiu", {"analeptic"}}, {"spear", {"slash"}}, {"DIT_duoshi", {"slash"}}, {"tenyearwusheng", {"slash"}}, {"heg_longdan", {"slash", "jink"}},
            {"longdan", {"slash"}}, {"qixi", {"dismantlement"}},
            {"nosgongqi", {"slash"}}, {"nosguose", {"indulgence"}}, {"duanliang", {"supply_shortage"}},
            {"zhanjue", {"duel"}}, {"olzhanjue", {"duel"}},
            {"gdlonghun", {"fire_slash"}},
            {"longhun", {"fire_slash"}}, {"newlonghun", {"fire_slash", "peach"}},
            {"jiuchi", {"analeptic"}},
            {"heg_kanpo", {"nullification"}}, {"jijiu", {"peach"}}, {"fan", {"fire_slash"}},
            {"heg_aozhan", {"slash", "jink"}}, {"heg_wusheng_xh", {"slash"}},
            {"heg_longdan_xh", {"slash", "jink"}},
            {"heg_miewu", {"nullification", "heg_nullification", "peach", "analeptic", "slash", "fire_slash", "thunder_slash", "jink"}},
            {"huangjinsymbol", {"slash"}}, {"heg_huangjinsymbol", {"slash"}}};
        static const QSet<QString> fields{"source_key", "skill", "name", "subcards", "suit", "number"};
        const QString key = spec.value("source_key").toString(), name = spec.value("name").toString();
        const bool olClanPlan = aiRound22OlClanConstruction(request, player, spec);
        const bool olClanAction = aiRound22OlClanActionConstruction(request, player, spec);
        const bool hc49Construction=aiRound49ExtraPlanningConstruction(request,m_room,player,spec);
        const bool r45Construction=aiRound45PlanningConstruction(request,m_room,player,spec);
        const bool r49ResponseConstruction=aiRound49ResponseConstruction(request,player,spec);
        const bool r49ResponseRecast=r49ResponseConstruction;
        const bool r48Construction=aiRound48PlanningConstruction(request,m_room,player,spec);
        const bool r48Recast=r48Construction && key=="newgenerals._xdfangchan";
        const bool r45Recast=r45Construction && key=="newgenerals._xdyifu";
        const bool r49MaotuYinhu = aiRound49MaotuYinhuConstruction(request,m_room,player,spec);
        const bool repeatedMaterials = (r49MaotuYinhu && key=="mtweiqie") || (olClanPlan && aiRound22OlClanRepeatedMaterials(spec)) || (r45Construction && aiRound45PlanningRepeatedMaterials(spec));
        const bool tyshouliConstruction = aiRound40PlanningConstruction(request, m_room, player, spec);
        const bool foreignMaterial = hc49Construction || aiRound49MaotuYinhuPublicMaterial(request,m_room,player,spec) || r45Construction || tyshouliConstruction || (olClanPlan && aiRound22OlClanForeignMaterial(spec))
            || aiRound24ChumingPublicMaterials(request, player, spec);
        const bool r42Construction = aiRound42PlanningConstruction(request,m_room,player,spec);
        const int sourceExtraTarget = olClanPlan ? aiRound22OlClanExtraTarget(spec)
            : r42Construction ? aiRound42PlanningExtraTarget(request,m_room,player,spec)
            : aiRound24FeiDIYExtraTarget(request, player, spec);
        const bool finiteSourceQueries = sourceExtraTarget == 99
            || (r42Construction && key == "newgenerals.mouxieji" && sourceExtraTarget == 2);
        // Original ableToSave clones a fresh NoSuitRed/0 Peach; this permits
        // only owner readonly rules, never foreign saver limits or payment.
        const auto saverFacts=request.worldView.strategyFacts.value("hegemony").toObject();
        const bool originalSaver=key=="original_able_to_save"
            && player && request.viewerObjectName==player->objectName()
            && request.stateRevision==m_room.roomRuntime()->stateRevision()
            && saverFacts.value("enabled")==QJsonValue(true)
            && saverFacts.value("original_overrides_loaded")==QJsonValue(true)
            && spec.size()==6 && spec.value("skill")==QJsonValue(QString())
            && spec.value("name")==QJsonValue(QStringLiteral("peach"))
            && spec.value("suit")==QJsonValue(int(Card::NoSuitRed))
            && spec.value("number")==QJsonValue(0)
            && spec.value("subcards").isArray() && spec.value("subcards").toArray().isEmpty();
        const bool originalGuanxing=aiRound35GuanxingConstruction(request,player,spec);
        const bool yhbozhiConstruction=aiRound49BozhiConstruction(request,m_room,player,spec);
        const bool saodongConstruction = aiRound47cSaodongConstruction(request,player,spec);
        const bool record49Construction=aiRound49RecordConstruction(request,m_room,player,spec);
        const bool auditedConstruction = aiRound53TychendeConstruction(request,m_room,player,spec) || aiRound53MingfaConstruction(request,m_room,player,spec) || aiRound53GengduConstruction(request,m_room,player,spec) || aiRound53TyfenwuConstruction(request,m_room,player,spec) || aiRound53TyqingkouConstruction(request,m_room,player,spec) || aiRound53ZhuningConstruction(request,m_room,player,spec) || aiRound53DiouConstruction(request,m_room,player,spec) || record49Construction || hc49Construction || aiRound49HTMSConstruction(request,m_room,player,spec) || r49MaotuYinhu || aiRound49ExtraConstruction(request,m_room,player,spec) || aiRound49JilieConstruction(request,m_room,player,spec) || aiRound49rHfgushiConstruction(request,m_room,player,spec) || aiRound49ZeroConstruction(request,m_room,player,spec) || yhbozhiConstruction || aiRound49QixiConstruction(request,m_room,player,spec) || aiRound49QhTurnConstruction(request,m_room,player,spec) || aiRound49GuhuoConstruction(request,m_room,player,spec) || aiRound49ShuangxiongConstruction(request,m_room,player,spec) || aiRound49LianhuanConstruction(request,m_room,player,spec) || aiRound49bResponseConstruction(request,player,spec) || r49ResponseConstruction || aiRound49bPlanningConstruction(request,m_room,player,spec) || aiRound49jPlanningConstruction(request,m_room,player,spec) || aiRound49PlanningConstruction(request,m_room,player,spec) || r48Construction || saodongConstruction || aiRound47XiedouConstruction(request,player,spec) || r45Construction || aiRound45KeolcihuangConstruction(request,m_room,player,spec) || aiRound45ShoushiConstruction(request,player,spec) || aiRound45InovationPhaseConstruction(request,player,spec) || aiRound45InovationConstruction(request, player, spec) || aiRound43ChoiceConstruction(request, player, spec) || aiAuditedConstructionQuery(request, player, spec) || aiRound41PlanningConstruction(request, m_room, player, spec) || aiRound43PlanningConstruction(request,m_room,player,spec) || aiRound44RangeConstruction(request,m_room,player,spec) || r42Construction || originalSaver || originalGuanxing || tyshouliConstruction
            || aiRound21PlanningConstruction(request, player, spec) || olClanPlan || olClanAction
            || aiRound23ChoiceConstruction(request, player, spec)
            || aiRound24ChoiceConstruction(request, player, spec)
            || aiRound24OriginalConstruction(request, player, spec)
            || aiRound25PlanningConstruction(request, player, spec)
            || aiRound25ChoiceConstruction(request, player, spec)
            || aiRound26ChoiceConstruction(request, player, spec)
            || aiRound27ChoiceConstruction(request, player, spec)
            || aiRound25ResponseConstruction(request, player, spec)
            || aiRound27ResponseConstruction(request, player, spec)
            || aiRound28ResponseConstruction(request, player, spec)
            || aiRound28XdResponseConstruction(request, player, spec)
            || aiRound29ResponseConstruction(request, player, spec)
            || aiRound39GodYouhuaConstruction(request, m_room, player, spec)
            || aiRound25OriginalConstruction(request, player, spec)
            || aiRound26OriginalConstruction(request, player, spec)
            || round25JincaiwangConstruction(request, player, spec);
        const bool conditional = key == "ntdtwo" || key == "ntdthree" || key == "luashipo"
            || key == "haidao" || key == "aoyi";
        if (conditional && (!spec.value("source_key").isString() || !spec.value("name").isString()
            || !spec.value("skill").isString()))
            return failure("conditional owner constructor identity is malformed");
        const bool tongyi = key == "tongyiTQ";
        const bool fuhui = key == "fuhui";
        const bool standardTurn = key == "longdan" || key == "qixi";
        if (standardTurn && (request.kind != AIRequest::Activate
            || request.reason != CardUseStruct::CARD_USE_REASON_PLAY))
            return failure("standard Longdan/Qixi source hypothesis is Play planning only");
        const bool standardMaterialTurn = key == "nosgongqi" || key == "nosguose" || key == "duanliang";
        const bool ordinaryJijiu = key == "jijiu" && request.kind == AIRequest::Activate
            && spec.value("suit") == int(Card::NoSuit) && spec.value("number") == 0;
        const bool fullHandDuel = key == "zhanjue" || key == "olzhanjue";
        const bool standardNoSuit = fullHandDuel || key == "nosguose" || ordinaryJijiu || key == "gdlonghun";
        if ((ordinaryJijiu || key == "jiuchi") && (request.kind != AIRequest::Activate
            || request.reason != CardUseStruct::CARD_USE_REASON_PLAY))
            return failure("Jijiu/Jiuchi source hypothesis is Play planning only");
        if (standardMaterialTurn && (request.kind != AIRequest::Activate
            || request.reason != CardUseStruct::CARD_USE_REASON_PLAY))
            return failure("ordinary source material hypothesis is Play planning only");
        if (standardNoSuit && (spec.value("suit") != int(Card::NoSuit) || spec.value("number") != 0))
            return failure("NosGuose source parser uses exact NoSuit/0 constructor");
        if (fullHandDuel && (request.kind != AIRequest::Activate
            || request.reason != CardUseStruct::CARD_USE_REASON_PLAY || player->isKongcheng()))
            return failure("Zhanjue source hypothesis requires current Play nonempty hand");
        if (key == "gdlonghun" && (request.kind != AIRequest::Activate
            || request.reason != CardUseStruct::CARD_USE_REASON_PLAY))
            return failure("GDLonghun detached source clone is Play planning only");
        const bool mountainTurn = key == "longhun" || key == "newlonghun";
        const bool mountainPair = key == "newlonghun" && spec.value("subcards").toArray().size() == 2;
        if (mountainTurn && (request.kind != AIRequest::Activate || request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            || (key == "longhun" && player->getHp() > 1)))
            return failure("Longhun source is outside the exact Play/Hp domain");
        if (mountainPair && (name != "fire_slash" || spec.value("suit") != int(Card::SuitToBeDecided) || spec.value("number") != 0))
            return failure("NewLonghun two-diamond source parser requires to_be_decided/0");
        const bool yanjiu = key == "LuaYanjiu";
        const bool sourceAnaleptic = yanjiu || key == "jieyou" || key == "oljiuchi" || key == "jiuchi";
        if ((fuhui || yanjiu || key == "melee_peach" || key == "noslonghun" || key == "jieyou" || key == "jianhun" || key == "GodQuxiang" || key == "GodGongshen" || key == "jiuxian" || key == "jiejiu") && (request.kind != AIRequest::Activate
            || request.reason != CardUseStruct::CARD_USE_REASON_PLAY))
            return failure("LuaYanjiu hypothetical conversion is turn planning only");
        const bool jiejiuName = key == "jiejiu" && request.worldView.strategyFacts.value("pattern_names").toArray().contains(name);
        const bool tongyiName = tongyi && (request.worldView.strategyFacts.value("pattern_names").toArray().contains(name)
            || aiRound33TongyiConstruction(request, m_room, player, spec));
        const bool fuhuiName = fuhui && request.worldView.strategyFacts.value("pattern_names").toArray().contains(name);
        const bool fuhuiRecast = fuhui && spec.contains("can_recast");
        const bool overseasRecast = aiRound27ChoiceConstruction(request, player, spec) && key == "ov_cairu" && spec.contains("can_recast");
        if (proposal.size() != 2 || spec.size() != fields.size() + int(fuhuiRecast || overseasRecast || r45Recast || r48Recast || r49ResponseRecast) || (!outputs.value(key).contains(name) && !tongyiName && !jiejiuName && !fuhuiName && !conditional && !auditedConstruction))
            return failure("owner view-as evaluation is outside audited source catalog");
        for (auto it = spec.begin(); it != spec.end(); ++it)
            if (!fields.contains(it.key()) && !((fuhuiRecast || overseasRecast || r45Recast || r48Recast || r49ResponseRecast) && it.key() == "can_recast")) return failure("invalid owner view-as field");
        const bool ditSpear = key == "DIT_duoshi";
        if (ditSpear && (request.kind != AIRequest::Activate || request.reason != CardUseStruct::CARD_USE_REASON_PLAY))
            return failure("DIT Spear hypothesis is turn planning only");
        const bool ordinarySpear = ditSpear || (key == "spear" && spec.value("skill").toString() == "spear");
        const bool ordinaryFan = key == "fan" && spec.value("skill").toString() == "fan";
        if (ordinaryFan && (request.kind != AIRequest::Activate
            || request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            || spec.value("suit") != int(Card::NoSuit) || spec.value("number") != 0))
            return failure("ordinary Fan hypothesis is the exact Play NoSuit/0 source clone");
        const QString producedSkill = (conditional || auditedConstruction) ? spec.value("skill").toString() : tongyi ? "tongyiAIWJ" : key == "fan" && !ordinaryFan ? "Fan" : key == "spear" && !ordinarySpear ? "Spear" : key;
        if (spec.value("skill").toString() != producedSkill) return failure("owner view-as product skill mismatch");
        bool visibleSkill = false;
        for (const Skill *skill : player->getVisibleSkillList(true))
            if (skill && skill->objectName() == key) visibleSkill = true;
        if (!auditedConstruction && (!visibleSkill || (!player->hasSkill(key) && !player->hasLordSkill(key)))
            && !((key == "fan" || key == "spear") && player->hasWeapon(key)))
            return failure("owner view-as source is not visible and owned");
        const QJsonArray costs = spec.value("subcards").toArray();
        const bool spear = key == "spear" || ditSpear;
        const bool heavenly = key == "huangjinsymbol" || key == "heg_huangjinsymbol";
        const Player *materialOwner = heavenly ? player->getLord() : player;
        if (!materialOwner) return failure("owner view-as lord material source is absent");
        int suit = -1, number = -1;
        const auto responseInteger = [](const QJsonValue &value, int &out) {
            const double n = value.toDouble();
            if (!value.isDouble() || !std::isfinite(n) || n != std::floor(n)
                || n < std::numeric_limits<int>::min() || n > std::numeric_limits<int>::max()) return false;
            out = int(n); return true;
        };
        if (!spec.value("subcards").isArray() || (!auditedConstruction && !fullHandDuel && (conditional ? costs.size() > 1 : fuhui ? costs.size() > 2 : costs.size() != (tongyi ? 0 : spear || mountainPair ? 2 : 1)))
            || (fuhui && (spec.value("suit") != int(Card::NoSuit) || spec.value("number") != 0))
            || (fuhuiRecast && (costs.size() != 2 || !spec.value("can_recast").isBool() || spec.value("can_recast").toBool()))
            || !responseInteger(spec.value("suit"), suit)
            || !responseInteger(spec.value("number"), number)
            || (tongyi && (suit != int(Card::NoSuit) || number != 0))
            || (spear && (number != 0 || (ordinarySpear
                ? (suit != int(Card::NoSuitBlack) && suit != int(Card::NoSuitRed) && suit != int(Card::NoSuit))
                : suit != int(Card::SuitToBeDecided)))))
            return failure("owner view-as material identity is malformed");
        QList<int> ids;
        for (const auto &cost : costs) {
            int id = -1;
            if (!responseInteger(cost, id) || id < 0 || (!repeatedMaterials && ids.contains(id)))
                return failure("owner view-as material identity is malformed");
            bool disclosed = foreignMaterial;
            if (aiRound49ExtraPileMaterial(request,m_room,player,spec,id)) {
                disclosed = true; // Exact current genjutsu own disclosed gensou:first only.
            } else if (aiRound41PlanningPileMaterial(request, m_room, player, spec, id)) {
                disclosed = true; // Exact own ShenJian prefix, live + already disclosed.
            } else if (key == "guangyu" && aiRound45InovationConstruction(request,player,spec)) {
                for(const auto&card:request.worldView.handCards)if(card.effectiveId==id)disclosed=true;
                for(const auto&card:request.worldView.self.equips)if(card.effectiveId==id)disclosed=true;
                for(const auto&card:request.worldView.self.judgingArea)if(card.effectiveId==id)disclosed=true;
                for(const auto&pile:request.worldView.self.piles)for(const auto&card:pile.cards)if(card.effectiveId==id)disclosed=true;
            } else if (key == "secondjincaiwang") {
                for (const AICardView &card : request.worldView.self.judgingArea)
                    if (card.effectiveId == id) disclosed = true;
            } else if (conditional && key == "luashipo") {
                // This exact source reads one lizhan card in response. A live
                // owner pile ID is insufficient without its already disclosed
                // face in this request; never turn the predicate into an oracle.
                if (player->getPile("lizhan").contains(id))
                    for (const AICardPileView &pile : request.worldView.self.piles)
                        if (pile.name == "lizhan")
                            for (const AICardView &card : pile.cards)
                                if (card.effectiveId == id) disclosed = true;
            } else if (heavenly) {
                // Only this named, viewer-disclosed lord pile is queryable.
                // A visible card elsewhere or a caller-supplied foreign owner
                // does not extend this read-only source boundary.
                const auto inspectPile = [&](const AIPlayerView &view) {
                    if (view.objectName != materialOwner->objectName()) return;
                    for (const AICardPileView &pile : view.piles)
                        if (pile.name == "heavenly_army")
                            for (const AICardView &card : pile.cards)
                                if (card.effectiveId == id) disclosed = true;
                };
                inspectPile(request.worldView.self);
                for (const AIPlayerView &view : request.worldView.players) inspectPile(view);
                if (!materialOwner->getPile("heavenly_army").contains(id)) disclosed = false;
            } else {
                for (const AICardView &card : request.worldView.handCards) if (card.effectiveId == id) disclosed = true;
                if (!spear || ordinarySpear) {
                    if (!spear)
                        for (const AICardView &card : request.worldView.self.equips) if (card.effectiveId == id) disclosed = true;
                    if (player->getHandPile().contains(id))
                        for (const AICardPileView &pile : request.worldView.self.piles)
                            for (const AICardView &card : pile.cards) if (card.effectiveId == id) disclosed = true;
                }
            }
            if ((fuhui || key == "jianhun" || key == "jiuxian" || key == "jiejiu" || key == "melee_peach") && !player->handCards().contains(id)) disclosed = false;
            if ((key == "GodQuxiang" || key == "GodGongshen") && !player->handCards().contains(id)
                && m_room.getCardPlace(id) != Player::PlaceEquip) disclosed = false;
            if ((yanjiu || key == "jieyou") && !player->handCards().contains(id)
                && !player->getPile("wooden_ox").contains(id)) disclosed = false;
            const Card *material = disclosed ? Sanguosha->getCard(id) : nullptr;
            if (!material || (!foreignMaterial && m_room.getCardOwner(id) != materialOwner)
                || (spear && m_room.getCardPlace(id) != Player::PlaceHand
                    && !(ordinarySpear && player->getHandPile().contains(id)))
                || (!spear && !mountainPair && !ordinaryFan && !standardNoSuit && !fuhui && !auditedConstruction && (material->getSuit() != suit || material->getNumber() != number)))
                return failure("owner view-as material is not disclosed unchanged owner state");
            if (key == "longdan" && ((!player->handCards().contains(id) && !player->getHandPile().contains(id))
                || !material->isKindOf("Jink")))
                return failure("Longdan source requires an owner hand/pile Jink");
            if (key == "qixi" && ((!player->handCards().contains(id) && !player->getEquipsId().contains(id)
                && !player->getHandPile().contains(id)) || !material->isBlack()))
                return failure("Qixi source requires an owner HE/pile black material");
            if (standardMaterialTurn && !player->handCards().contains(id) && !player->getEquipsId().contains(id)
                && !player->getHandPile().contains(id))
                return failure("ordinary source material is outside owner HE/pile");
            if (key == "nosgongqi" && material->getTypeId() != Card::TypeEquip)
                return failure("NosGongqi source requires equipment material");
            if (key == "nosguose" && material->getSuit() != Card::Diamond)
                return failure("NosGuose source requires diamond material");
            if (key == "duanliang" && (!material->isBlack()
                || (!material->isKindOf("BasicCard") && !material->isKindOf("EquipCard"))))
                return failure("Duanliang source requires black Basic/Equip material");
            if (mountainTurn) {
                const bool inHand = player->handCards().contains(id) || player->getHandPile().contains(id);
                const bool inEquip = player->getEquipsId().contains(id);
                if (!inHand && (!inEquip || (key == "newlonghun" && !material->isKindOf("DefensiveHorse") && !material->isKindOf("OffensiveHorse"))))
                    return failure("Longhun source material is outside its exact owner zones");
                if ((name == "fire_slash" && material->getSuit() != Card::Diamond)
                    || (name == "peach" && material->getSuit() != Card::Heart))
                    return failure("Longhun source material suit does not match product");
            }
            if (ordinaryJijiu && ((!player->handCards().contains(id) && !player->getEquipsId().contains(id)) || !material->isRed()))
                return failure("Jijiu fill source requires owner HE red material");
            if (key == "jiuchi" && ((!player->handCards().contains(id) && !player->getHandPile().contains(id))
                || material->getSuit() != Card::Spade))
                return failure("Jiuchi fill source requires owner hand/pile spade material");
            if (key == "gdlonghun" && !player->handCards().contains(id)
                && !player->getEquipsId().contains(id) && !player->getHandPile().contains(id))
                return failure("GDLonghun source requires current owner HE/pile material");
            // Source clones before testing Diamond: readonly may use another
            // suit, while the native selected provider independently rejects it.
            if (ordinaryFan && ((!player->handCards().contains(id) && !player->getHandPile().contains(id))
                || !material->isKindOf("Slash") || material->isKindOf("NatureSlash")))
                return failure("ordinary Fan source requires an owner hand/pile normal Slash");
            if (yanjiu && !player->property("LuaYanjiu").toString().split(QChar('+')).contains(material->getSuitString()))
                return failure("LuaYanjiu material suit is outside the current owner declaration");
            if (key == "guihao" && (!material->isBlack() || material->isEquipped()))
                return failure("Guihao knowledge requires a black unequipped owner material");
            if (key == "melee_peach" && !material->isKindOf("Peach"))
                return failure("Melee Peach hypothesis requires a Peach material");
            if (key == "noslonghun" && (player->getHp() > 1 || material->getSuit() != Card::Diamond))
                return failure("Noslonghun fire Slash source prerequisites changed");
            if (key == "jieyou" && (material->getSuit() != Card::Spade || material->getNumber() < 2 || material->getNumber() > 9))
                return failure("Jieyou hypothesis requires spade 2 through 9");
            ids.append(id);
        }
        if (fullHandDuel && ids != player->handCards())
            return failure("Zhanjue source hypothesis must retain the entire current hand in source order");
        // This is the readonly predicate, not the final product admission
        // predicate: it additionally admits Aoyi's unskilled NoSuitRed/0 target
        // probe. Every skill/cost/face branch is finite and live-owner checked.
        if (conditional && !aiAuditedConditionalOrdinaryPreview(request, key, producedSkill,
                name, suit, number, ids, player))
            return failure("conditional owner constructor is outside its current source branch");
        EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
        LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
        // Actual MethodNone askForCard preserves UNKNOWN in RoomState; the
        // generic request label is RESPONSE. Keep this finite source query exact.
        ResponseNativeScope responseScope(m_room, player,
            (saodongConstruction || (r49MaotuYinhu && key=="mtweiqie")) ? CardUseStruct::CARD_USE_REASON_UNKNOWN : request.reason, request.pattern);
        // Ordinary cardsView_spear builds a NoSuit/0 dummy, then toString
        // records its material-derived suit. It may still be only a hypothetical
        // candidate: using/jilei/native prefix admission belongs to final bind.
        // The literal Slash constructor owns an initially empty QVariantMap.
        // Establish this before construction/query callbacks, never by reading
        // the resulting tag. The complete R56 source union must still be live
        // when this card-local fact is published below.
        const bool round56FreshSlashDamage = record49Construction && key == "choulve"
            && name == "slash" && inAIRound56ProjectionScope(m_room, round56Lua)
            && Sanguosha->aiRound56StandardCloneFactory(name);
        std::unique_ptr<Card> card(Sanguosha->cloneCard(name,
            ordinarySpear ? Card::NoSuit : Card::Suit(suit), number));
        if (!card && record49Construction) { if (!spec.value("skill").toString().isEmpty() || !spec.value("subcards").toArray().isEmpty()) return failure("only the fresh record literal clone may be absent"); if (!aiRound49RecordConstruction(request,m_room,player,spec)) return failure("record literal clone changed its source scope"); return {{"kind","owner_view_as_evaluation"},{"complete",true},{"exists",false}}; }
        if (!card && (originalGuanxing || yhbozhiConstruction || (r49MaotuYinhu && key=="yhduwei") || (key == "@@s_newtypeOther" && auditedConstruction))) return {{"kind","owner_view_as_evaluation"},{"complete",true},{"exists",false}};
        if (!card) return failure("owner view-as printed class is unavailable");
        if(record49Construction && key=="choulve")recordAIRound56ProjectionSourceCard(m_room,card.get(),name);
        if (key == "jiejiu" && !card->isKindOf("BasicCard")) return failure("Jiejiu hypothesis is not a BasicCard");
        if (r45Construction && !aiRound45PlanningProduct(spec,*card))
            return failure("ordered source product class differs");
        card->setSkillName(producedSkill); card->addSubcards(ids);
        if (olClanAction && !applyAIRound22OlClanActionConstruction(request, player, spec, *card))
            return failure("OlClan detached source hypothesis differs");
        std::unique_ptr<AITianyiQueryScope> extraTargetScope;
        if (sourceExtraTarget) extraTargetScope.reset(new AITianyiQueryScope(player, card.get(), sourceExtraTarget, false));
        if (ordinarySpear && (int(card->getSuit()) != suit || card->getNumber() != number))
            return failure("ordinary Spear printed identity differs from disclosed materials");
        if (key == "heg_miewu" || key == "heg_miewu_plan" || key == "heg_guishu" || fuhuiRecast || overseasRecast || r45Recast || r48Recast || r49ResponseRecast) card->setCanRecast(false);
        if (fuhui && !costs.isEmpty() && !(card->isKindOf("BasicCard") || card->isNDTrick()))
            return failure("Fuhui source only adds materials after its Basic/NDTrick check");
        const bool sourceAnalepticAvailable = sourceAnaleptic && Analeptic::IsAvailable(player, card.get());
        const auto method = card->getHandlingMethod();
        const bool limited = player->isCardLimited(card.get(), method);
        QJsonObject row = publicDecisionCard(card.get(), true);
        row.insert("evaluation_only", true); row.insert("evaluation_owner", player->objectName());
        row.insert("virtual_card", true); row.insert("subcards", costs); row.insert("handling_method", int(method));
        // This finite evaluator has already authorized every disclosed material;
        // preserve its effective identity for the actual fill/request index.
        if (!costs.isEmpty()) row.insert("effective_id", card->getEffectiveId());
        row.insert("can_recast", card->canRecast());
        if (mountainPair || fuhui || ordinaryFan || standardNoSuit || auditedConstruction) row.insert("source_constructor", spec);
        if (sourceAnaleptic) row.insert("source_analeptic_available", sourceAnalepticAvailable);
        if (r48Construction && key=="newgenerals._yantaoluan")
            row.insert("source_yantaoluan_can_slash",aiRound48YantaoluanSlash(request,m_room,player,spec,card.get()));
        const bool responseQuery = (request.kind == AIRequest::UseCard || request.kind == AIRequest::RespondCard)
            && key != "@@maware"; // Fixed source runs ordinary aiUseCard on its hypothesis, not @@ pattern matching.
        QString responsePattern = request.pattern;
        if (responsePattern.endsWith(QLatin1Char('!'))) responsePattern.chop(1);
        if (responseQuery && !responsePattern.isEmpty())
            row.insert("matches_response_pattern", Sanguosha->matchExpPattern(responsePattern, player, card.get()));
        row.insert("legacy_string_key", card->toString()); row.insert("legacy_sort_key", legacySortKey(card.get()));
        // Original getCards compares this exact product's target modifiers and
        // may pass it through a dummy use before choosing any real conversion.
        // These are query results for the same skill/material clone, not rules
        // borrowed from an unmodified printed Slash and not an action receipt.
        auto sourceRules = responseOwnerRules(m_room, player, card.get());
        if(originalSaver) {
            // The third argument is essential: source asks as a hand card.
            QJsonObject handLimits=sourceRules.value("limits_hand").toObject();
            handLimits.insert(QString::number(int(Card::MethodUse)),player->isCardLimited(card.get(),Card::MethodUse,true));
            sourceRules.insert("limits_hand",handLimits);
        }

        if (sourceExtraTarget) sourceRules.insert("target_mod_extra", sourceRules.value("target_mod_extra").toInt() - sourceExtraTarget);
        row.insert("evaluation_rules", sourceRules);
        row.insert("target_residue", responseResidueJson(responseResidue(m_room, player, card.get())));
        row.insert("locked", player->isLocked(card.get())); row.insert("using", card->hasFlag("using"));
        int budget = 4096;
        bool sourceQueriesComplete = false;
        auto candidate = makeAICardCandidate(m_room, player, card.get(),
            responseQuery ? request.handlingMethod : Card::MethodUse,
            responseQuery ? request.kind : AIRequest::Activate,
            responseQuery ? responsePattern : QString(), budget,
            finiteSourceQueries, &sourceQueriesComplete);
        if (finiteSourceQueries && !sourceQueriesComplete)
            return failure("Qiuxin finite source queries are incomplete");
        if (sourceExtraTarget) {
            if (candidate.extraTargetKnown) candidate.extraTargetCount -= sourceExtraTarget;
            for (auto it = candidate.extraTargetByPlayer.begin(); it != candidate.extraTargetByPlayer.end(); ++it) it.value() -= sourceExtraTarget;
        }
        auto targets = responseCandidateJson(candidate);
        if (finiteSourceQueries) targets.insert("readonly_source_queries", QJsonObject{{"source_key", key}, {"extra_target", sourceExtraTarget}});
        for (auto it = targets.begin(); it != targets.end(); ++it) row.insert(it.key(), it.value());
        if (!aiRound44RangeRequestCurrent(m_room,parentRequest))
            return failure("owner view-as evaluation changed state");
        const auto recipeKey = m_room.roomRuntime()->ai().issueReadonlyCard(parentRequest, player, std::move(card), key);
        if (!recipeKey.isEmpty()) row.insert("readonly_recipe_key", recipeKey);
        if (round56FreshSlashDamage && !recipeKey.isEmpty()
            && inAIRound56ProjectionScope(m_room, round56Lua))
            row.insert("source_damage_state", QJsonObject{{"midao_nature", QString()},
                {"basis", "r56_native_slash_constructor"}});
        return {{"kind", "owner_view_as_evaluation"}, {"complete", true}, {"card", row},
                {"candidate", targets}, {"limited", limited}, {"handling_method", int(method)}};
    }
    if (proposal.value("kind").toString() == "evaluate_skill_card") {
        if (proposal.value("skill")=="limu_source") {
            if (proposal.size()!=4 || !proposal.value("name").isString()
                || !proposal.value("subcards").isArray()) return failure("Limu source hypothesis malformed");
            QList<int> costs;
            for (const auto &v:proposal.value("subcards").toArray()) {
                const double id=v.toDouble(-1);
                if(!v.isDouble() || id<0 || id!=std::floor(id) || id>std::numeric_limits<int>::max())
                    return failure("Limu source material ID malformed");
                costs<<int(id);
            }
            EngineRuntimeContextScope engine(*Sanguosha,&m_room);
            LuaRuntime::Binding game(m_room.roomRuntime()->lua(),false);
            ResponseNativeScope native(m_room,player,request.reason,request.pattern);
            QString error;
            auto card=aiRound42LimuEvaluation(request,m_room,player,proposal.value("name").toString(),costs,&error);
            if(!card)return failure(error);
            int budget=4096;
            const auto candidate=makeAICardCandidate(m_room,player,card.get(),Card::MethodUse,AIRequest::Activate,{},budget);
            QJsonObject row=publicDecisionCard(card.get(),true);
            row.insert("id",card->getId());row.insert("effective_id",card->getEffectiveId());
            row.insert("virtual_card",card->isVirtualCard());row.insert("subcards",responseIds(card->getSubcards()));
            row.insert("evaluation_only",true);row.insert("evaluation_owner",player->objectName());
            row.insert("evaluation_rules",responseOwnerRules(m_room,player,card.get()));
            row.insert("target_residue",responseResidueJson(responseResidue(m_room,player,card.get())));
            row.insert("locked",player->isLocked(card.get()));row.insert("using",card->hasFlag("using"));
            row.insert("jilei",player->isJilei(card.get()));row.insert("handling_method",int(card->getHandlingMethod()));
            row.insert("can_recast",card->canRecast());row.insert("legacy_string_key",card->toString());
            row.insert("legacy_sort_key",legacySortKey(card.get()));
            const auto targets=responseCandidateJson(candidate);
            for(auto i=targets.begin();i!=targets.end();++i)row.insert(i.key(),i.value());
            if(!aiRound44RangeRequestCurrent(m_room,parentRequest))return failure("Limu readonly evaluation changed gameplay state");
            const auto key=m_room.roomRuntime()->ai().issueReadonlyCard(parentRequest,player,std::move(card),"limu_source");
            if(key.isEmpty())return failure("Limu readonly evaluation lacks native custody");
            row.insert("readonly_recipe_key",key);
            return {{"card",row},{"candidate",targets}};
        }

        if (proposal.value("skill")=="GodYouhua"
            && (proposal.value("name")=="source_reference" || proposal.value("name")=="source_parse"))
            return queryAIRound39GodYouhua(request,m_room,player,proposal);
        // Fixed olddiy819: mutate only an already-disclosed physical policy
        // object. Its ID and legacy identity stay physical; this mints no ticket.
        if (proposal.value("skill")=="physical:bu_stwo_guicai") {
            const auto values=proposal.value("subcards").toArray();
            const double raw=values.size()==1?values.first().toDouble(-1):-1;
            if(proposal.size()!=4||proposal.value("name")!="bu_stwo_guicai"||!proposal.value("subcards").isArray()
                ||values.size()!=1||!values.first().isDouble()||raw<0||raw!=std::floor(raw)
                ||raw>std::numeric_limits<int>::max())return failure("Guicai physical evaluation shape differs");
            EngineRuntimeContextScope contextScope(*Sanguosha,&m_room);
            LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(),false);
            ResponseNativeScope responseScope(m_room,player,request.reason,request.pattern);
            QString error;QJsonObject row;
            auto card=aiRound39GuicaiEvaluation(request,m_room,player,int(raw),&row,&error);
            if(!card)return failure(error);
            int budget=4096;
            auto candidate=makeAICardCandidate(m_room,player,card.get(),Card::MethodUse,AIRequest::Activate,QString(),budget);
            row.insert("evaluation_only",true);row.insert("evaluation_owner",player->objectName());
            row.insert("skill_name","bu_stwo_guicai");row.insert("flags_complete",false);
            row.insert("subcards",responseIds(card->getSubcards()));
            row.insert("can_recast",card->canRecast());row.insert("target_fixed",card->targetFixed());
            row.insert("handling_method",int(card->getHandlingMethod()));
            row.insert("locked",player->isLocked(card.get()));
            row.insert("target_residue",responseResidueJson(responseResidue(m_room,player,card.get())));
            row.insert("evaluation_rules",responseOwnerRules(m_room,player,card.get()));
            const auto targets=responseCandidateJson(candidate);
            for(auto it=targets.begin();it!=targets.end();++it)row.insert(it.key(),it.value());
            if(!aiRound44RangeRequestCurrent(m_room,parentRequest))return failure("Guicai evaluation changed native state");
            const auto key=m_room.roomRuntime()->ai().issueReadonlyCard(parentRequest,player,std::move(card),"physical:bu_stwo_guicai");
            if(!key.isEmpty())row.insert("readonly_recipe_key",key);
            return{{"card",row},{"candidate",targets}};
        }
        // Fixed htms6346: mutate only an already-disclosed physical policy
        // object. Its ID and legacy identity stay physical; this mints no ticket.
        if (proposal.value("skill")=="physical:zhongerhx") {
            const auto values=proposal.value("subcards").toArray();
            const double raw=values.size()==1?values.first().toDouble(-1):-1;
            if(proposal.size()!=4||proposal.value("name")!="zhongerhx"||!proposal.value("subcards").isArray()
                ||values.size()!=1||!values.first().isDouble()||raw<0||raw!=std::floor(raw)
                ||raw>std::numeric_limits<int>::max())return failure("Zhongerhx physical evaluation shape differs");
            EngineRuntimeContextScope contextScope(*Sanguosha,&m_room);
            LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(),false);
            ResponseNativeScope responseScope(m_room,player,request.reason,request.pattern);
            QString error;QJsonObject row;
            auto card=aiRound38ZhongerhxEvaluation(request,m_room,player,int(raw),&row,&error);
            if(!card)return failure(error);
            int budget=4096;
            auto candidate=makeAICardCandidate(m_room,player,card.get(),Card::MethodUse,AIRequest::Activate,QString(),budget);
            row.insert("evaluation_only",true);row.insert("evaluation_owner",player->objectName());
            row.insert("skill_name","zhongerhx");row.insert("flags_complete",false);
            row.insert("subcards",responseIds(card->getSubcards()));
            row.insert("can_recast",card->canRecast());row.insert("target_fixed",card->targetFixed());
            row.insert("handling_method",int(card->getHandlingMethod()));
            row.insert("locked",player->isLocked(card.get()));
            row.insert("target_residue",responseResidueJson(responseResidue(m_room,player,card.get())));
            row.insert("evaluation_rules",responseOwnerRules(m_room,player,card.get()));
            const auto targets=responseCandidateJson(candidate);
            for(auto it=targets.begin();it!=targets.end();++it)row.insert(it.key(),it.value());
            if(!aiRound44RangeRequestCurrent(m_room,parentRequest))return failure("Zhongerhx evaluation changed native state");
            const auto key=m_room.roomRuntime()->ai().issueReadonlyCard(parentRequest,player,std::move(card),"physical:zhongerhx");
            if(!key.isEmpty())row.insert("readonly_recipe_key",key);
            return{{"card",row},{"candidate",targets}};
        }
        // Fixed Qiangxi/Tiaoxin revise hypotheses. Real V1 constructors,
        // current owner disclosed materials; no V2 payment/activation checks.
        if (aiRound33ChoiceEvaluationSource(proposal.value("skill").toString())) {
            if (proposal.size()!=4 || !proposal.value("name").isString()
                || !proposal.value("subcards").isArray())
                return failure("use-revise evaluation proposal shape differs");
            EngineRuntimeContextScope contextScope(*Sanguosha,&m_room);
            LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(),false);
            ResponseNativeScope responseScope(m_room,player,request.reason,request.pattern);
            QString error;
            const QString source=proposal.value("skill").toString();
            auto card=aiRound33ChoiceEvaluation(request,m_room,player,source,
                proposal.value("name").toString(),proposal.value("subcards").toArray(),&error);
            if (!card) return error.isEmpty() ? QJsonObject{{"exists",false}} : failure(error);
            int budget=4096;
            auto candidate=makeAICardCandidate(m_room,player,card.get(),Card::MethodUse,
                AIRequest::Activate,QString(),budget);
            QJsonObject row=publicDecisionCard(card.get(),true);
            row.insert("evaluation_only",true);row.insert("virtual_card",card->isVirtualCard());
            row.insert("evaluation_owner",player->objectName());
            row.insert("effective_id",card->getEffectiveId());row.insert("id",card->getId());
            row.insert("subcards",responseIds(card->getSubcards()));
            row.insert("can_recast",card->canRecast());row.insert("target_fixed",card->targetFixed());
            row.insert("handling_method",int(card->getHandlingMethod()));
            row.insert("locked",player->isLocked(card.get()));row.insert("using",card->hasFlag("using"));
            row.insert("legacy_string_key",card->toString());row.insert("legacy_sort_key",legacySortKey(card.get()));
            row.insert("target_residue",responseResidueJson(responseResidue(m_room,player,card.get())));
            row.insert("evaluation_rules",responseOwnerRules(m_room,player,card.get()));
            const QJsonObject targetData=responseCandidateJson(candidate);
            for(auto it=targetData.begin();it!=targetData.end();++it)row.insert(it.key(),it.value());
            if(!aiRound44RangeRequestCurrent(m_room,parentRequest))
                return failure("use-revise evaluation changed the current game state");
            const auto key=m_room.roomRuntime()->ai().issueReadonlyCard(parentRequest,player,std::move(card),source);
            if(!key.isEmpty())row.insert("readonly_recipe_key",key);
            return {{"card",row},{"candidate",targetData}};
        }
        if (proposal.value("skill")=="tenyearbenxi:extra_collateral") {
            if(proposal.size()!=4 || !proposal.value("name").isString()
                || !proposal.value("subcards").isArray() || !proposal.value("subcards").toArray().isEmpty())
                return failure("Benxi parser query differs from the exact source call");
            EngineRuntimeContextScope contextScope(*Sanguosha,&m_room);
            LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(),false);
            const QString wire=proposal.value("name").toString(); bool authorized=false;
            const Card*parsed=parseAIRound51Collateral(parentRequest,m_room,player,wire,&authorized);
            if(!authorized)return failure("Benxi parser has no current native producer/question witness");
            if(!parsed)return {{"exists",false}};
            std::unique_ptr<Card> owned(parsed->isVirtualCard()?const_cast<Card*>(parsed):nullptr);
            QJsonObject row=publicDecisionCard(parsed,false);
            row.insert("evaluation_only",true);row.insert("evaluation_owner",player->objectName());
            row.insert("virtual_card",parsed->isVirtualCard());row.insert("id",parsed->getId());
            row.insert("effective_id",parsed->getEffectiveId());row.insert("subcards",responseIds(parsed->getSubcards()));
            row.insert("legacy_string_key",parsed->toString());row.insert("legacy_sort_key",legacySortKey(parsed));
            // No hypothetical rule/flag profile: the reached source first tests
            // this actual Card getter. A nonstandard damage-card path retains
            // its existing downstream target-query boundary.
            row.insert("damage_card",parsed->isDamageCard());
            if(!aiRound51CollateralCurrent(parentRequest,m_room,player,wire))
                return failure("Benxi parser observation changed the current question");
            const QString key=owned
                ? m_room.roomRuntime()->ai().issueReadonlyCard(parentRequest,player,std::move(owned),"tenyearbenxi:extra_collateral")
                : m_room.roomRuntime()->ai().issueBorrowedReadonlyCard(parentRequest,player,parsed,"tenyearbenxi:extra_collateral");
            if(key.isEmpty())return failure("Benxi parsed object could not retain its readonly identity");
            row.insert("readonly_recipe_key",key);
            return {{"card",row},{"candidate",QJsonObject()}};
        }
        // The fixed Overseas delegates parse these base prototypes only to
        // pass a card through the base strategy body. They never acquire the
        // base skill or payment authority; the caller selects its Overseas card.
        const QString delegateSkill = proposal.value("skill").toString();
        if (delegateSkill == "ov_jinglve" || delegateSkill == "ov_gongxin") {
            const QString prototype = delegateSkill == "ov_jinglve" ? "JinglveCard" : "GongxinCard";
            if (proposal.size() != 4 || proposal.value("name") != prototype
                || !proposal.value("subcards").isArray() || !proposal.value("subcards").toArray().isEmpty()
                || request.kind != AIRequest::Activate || request.reason != CardUseStruct::CARD_USE_REASON_PLAY
                || !player->isAlive() || player->getPhase() != Player::Play || !player->hasSkill(delegateSkill))
                return failure("Overseas base delegate is outside its readonly source shape");
            EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
            LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
            const Card *parsed = Card::Parse("@" + prototype + "=.");
            if (!parsed) return {{"exists", false}};
            if (!parsed->isVirtualCard()) return failure("Overseas base delegate did not parse a virtual prototype");
            std::unique_ptr<Card> card(const_cast<Card *>(parsed));
            if (card->getClassName() != prototype || !card->getSubcards().isEmpty())
                return failure("Overseas base delegate prototype changed");
            QJsonObject row = publicDecisionCard(card.get(), true);
            row.insert("evaluation_only", true); row.insert("virtual_card", true);
            row.insert("evaluation_owner", player->objectName());
            row.insert("subcards", QJsonArray()); row.insert("can_recast", card->canRecast());
            row.insert("effective_id", card->getEffectiveId()); row.insert("id", card->getId());
            row.insert("legacy_string_key", card->toString()); row.insert("legacy_sort_key", legacySortKey(card.get()));
            row.insert("target_fixed", card->targetFixed()); row.insert("handling_method", int(card->getHandlingMethod()));
            if (!aiRound44RangeRequestCurrent(m_room,parentRequest))
                return failure("Overseas base delegate parsing changed state");
            return {{"evaluation_only", true}, {"card", row}, {"candidate", QJsonObject()}};
        }
        const QString fcSource = proposal.value("skill").toString();
        if (fcSource.startsWith("fcDIY_context:") || fcSource=="newgenerals_context:moujiang") {
            if (proposal.size()!=4 || !proposal.value("name").isString()
                || !proposal.value("subcards").isArray() || !proposal.value("subcards").toArray().isEmpty())
                return failure("FC actual-card query shape differs");
            EngineRuntimeContextScope contextScope(*Sanguosha,&m_room);
            LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(),false);
            ResponseNativeScope responseScope(m_room,player,request.reason,request.pattern);
            const Card *card=fcSource=="newgenerals_context:moujiang" ? aiRound45MoujiangEvaluation(request,m_room,player) : aiRound28PlanningEvaluation(request,m_room,player,fcSource);
            if (!card || card->objectName()!=proposal.value("name").toString())
                return failure("FC current public use is unavailable or changed");
            const int extra=fcSource=="newgenerals_context:moujiang" ? 99 : aiRound28PlanningExtraTarget(request,m_room,player,fcSource);
            AITianyiQueryScope extraScope(player,card,extra,false);
            auto row=publicDecisionCard(card);
            row.insert("evaluation_only",true);row.insert("evaluation_owner",player->objectName());
            auto rules=responseOwnerRules(m_room,player,card);
            rules.insert("target_mod_extra",rules.value("target_mod_extra").toInt()-extra);
            row.insert("evaluation_rules",rules);
            row.insert("target_residue",responseResidueJson(responseResidue(m_room,player,card)));
            row.insert("target_fixed",card->targetFixed());row.insert("can_recast",card->canRecast());
            row.insert("handling_method",int(card->getHandlingMethod()));row.insert("virtual_card",card->isVirtualCard());
            row.insert("subcards",responseIds(card->getSubcards()));row.insert("locked",player->isLocked(card));
            const auto actualUse=m_room.getTag(fcSource=="newgenerals_context:moujiang" ? "moujiang" : fcSource.mid(14)).value<CardUseStruct>();
            if(actualUse.from==player)row.insert("using",card->hasFlag("using"));
            row.insert("legacy_string_key",card->toString());row.insert("legacy_sort_key",legacySortKey(card));
            int budget=4096;bool sourceQueriesComplete=false;
            auto candidate=makeAICardCandidate(m_room,player,card,Card::MethodUse,AIRequest::Activate,
                QString(),budget,true,&sourceQueriesComplete);
            if (!sourceQueriesComplete) return failure("FC source queries are incomplete");
            if (candidate.extraTargetKnown) candidate.extraTargetCount-=extra;
            for(auto it=candidate.extraTargetByPlayer.begin();it!=candidate.extraTargetByPlayer.end();++it)it.value()-=extra;
            auto targets=responseCandidateJson(candidate);
            targets.insert("readonly_source_queries",QJsonObject{{"source_key",fcSource},{"extra_target",extra}});
            for(auto it=targets.begin();it!=targets.end();++it)row.insert(it.key(),it.value());
            if(!aiRound44RangeRequestCurrent(m_room,parentRequest))return failure("FC query changed state");
            const auto key=m_room.roomRuntime()->ai().issueBorrowedReadonlyCard(parentRequest,player,card,fcSource);
            if(key.isEmpty())return failure("FC actual-card observation lifetime unavailable");
            row.insert("readonly_recipe_key",key);
            return {{"evaluation_only",true},{"card",row},{"candidate",targets}};
        }
        if (proposal.value("skill").toString() == "luacaokong_context") {
            if (proposal.size() != 4 || !proposal.value("name").isString()
                || !proposal.value("subcards").isArray() || !proposal.value("subcards").toArray().isEmpty())
                return failure("invalid Caokong actual-card query");
            EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
            LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
            ResponseNativeScope responseScope(m_room, player, request.reason, request.pattern);
            const Card *card = aiRound26PlanningEvaluation(request, m_room, player, "luacaokong_context");
            if (!card || card->objectName() != proposal.value("name").toString())
                return failure("Caokong actual card is not lawfully disclosed in this question");
            int budget = 4096;
            const auto candidate = makeAICardCandidate(m_room, player, card, Card::MethodUse,
                AIRequest::Activate, QString(), budget);
            const auto rules = responseOwnerRules(m_room, player, card);
            const auto targets = responseCandidateJson(candidate);
            QJsonObject row = publicDecisionCard(card, m_room.getCardOwner(card->getEffectiveId()) == player);
            row.insert("evaluation_only", true); row.insert("evaluation_owner", player->objectName());
            row.insert("evaluation_rules", rules);
            row.insert("target_residue", responseResidueJson(responseResidue(m_room, player, card)));
            row.insert("locked", player->isLocked(card));
            if (m_room.getCardOwner(card->getEffectiveId()) == player) row.insert("using", card->hasFlag("using"));
            row.insert("can_recast", card->canRecast()); row.insert("handling_method", int(card->getHandlingMethod()));
            row.insert("virtual_card", false); row.insert("subcards", QJsonArray());
            row.insert("legacy_string_key", card->toString()); row.insert("legacy_sort_key", legacySortKey(card));
            for (auto it = targets.begin(); it != targets.end(); ++it) row.insert(it.key(), it.value());
            if (!aiRound44RangeRequestCurrent(m_room,parentRequest)) return failure("Caokong evaluation changed state");
            return {{"evaluation_only", true}, {"card", row}, {"card_rules", rules}, {"candidate", targets}};
        }
        const QString skillName = proposal.value("skill").toString();
        const QString name = proposal.value("name").toString();
        const bool printedPolicy = skillName.isEmpty();
        const bool chouliePolicy = aiRound48ChoulieEvaluation(parentRequest, player, proposal);
        const bool r52OlspPolicy = aiRound52OlspEvaluation(parentRequest, player, proposal);
        const bool renchouPolicy = aiRound49qRenchouEvaluation(parentRequest, player, proposal);
        const bool renchouLimit = renchouPolicy && skillName == "renchou_limit";
        const bool r50Policy = aiRound50PlanningEvaluation(request,m_room,player,proposal);
        const bool panqinPolicy = aiRound49ePanqinEvaluation(parentRequest, player, proposal);
        const bool xixiangPolicy = aiRound49dXixiangEvaluation(parentRequest, player, proposal);
        const bool fenyuePolicy = aiRound21FenyueEvaluation(request, skillName, name, proposal.value("subcards").toArray());
        const bool taozeiTmPolicy = aiRound28PlanningConstruction(request,m_room,player,
            {{"name",name},{"skill",skillName},{"suit",int(Card::NoSuit)},{"number",0},{"subcards",proposal.value("subcards")}});
        static const QMap<QString, QString> nyBeautyClones{
            {"nymiaojian", "_stabs_slash"}, {"nymansi", "savage_assault"},
            {"nywuniang", "slash"}, {"ny_zhenge", "slash"}, {"nyarz_zhifou", "peach"}};
        const bool nyTongli = skillName == "nytongli";
        const bool nyBeautyPolicy = nyBeautyClones.contains(skillName) || nyTongli;
        if (nyBeautyPolicy && (nyTongli
                ? request.kind != AIRequest::UseCard || request.pattern != "@@nytongli"
                    || player->property("nytonglipattern").toString() != name
                : nyBeautyClones.value(skillName) != name))
            return failure("Nybeauty readonly clone is outside its fixed source shape");
        const bool nyarzPolicy = skillName == "nyarz_zhishi" || skillName == "nyarz_baguan";
        const bool tousuiPolicy = skillName == "tousui";
        const bool erangwaiPolicy = skillName == "erangwai";
        const bool wuxingPolicy = skillName == "wuxinghelingshan";
        const bool faqiPolicy = skillName == "faqi";
        if (faqiPolicy) {
            bool offered = false;
            if (request.kind == AIRequest::AmazingGrace && request.choiceOptions.reason == "faqi")
                for (const auto &card : request.choiceOptions.cards) if (card.objectName == name) offered = true;
            if (!offered) return failure("Faqi hypothetical name is outside the current revealed choice");
        }

        if (tousuiPolicy && request.kind != AIRequest::Activate)
            return failure("Tousui hypothetical Slash is turn planning only");
        if (erangwaiPolicy && (request.kind != AIRequest::UseCard || request.pattern != "@@erangwai"))
            return failure("Erangwai hypothetical Slash is outside its response question");
        if (wuxingPolicy && (request.kind != AIRequest::Activate || !player->hasWeapon("wuxinghelingshan")
            || !request.worldView.strategyFacts.value("pattern_names").toArray().contains(name)))
            return failure("Wuxing hypothesis is outside the equipped owner turn");
        if (nyarzPolicy) {
            const QString reason = request.choiceOptions.reason;
            const bool slashChoice = request.kind == AIRequest::Choice && reason == skillName + "_slash"
                && (name == "slash" || name == "fire_slash" || name == "thunder_slash");
            const bool declaredChoice = request.kind == AIRequest::Choice && skillName == "nyarz_zhishi"
                && reason == skillName && request.choiceOptions.choices.contains(name);
            const bool declaredUse = request.kind == AIRequest::UseCard && request.pattern == "@@" + skillName
                && player->property(skillName.toUtf8().constData()).toString() == name;
            if (!slashChoice && !declaredChoice && !declaredUse)
                return failure("Nyarz readonly declaration is outside the active owner question");
        }
        if (printedPolicy) {
            const auto names = request.worldView.strategyFacts.value("pattern_names").toArray();
            static const QSet<QString> commonPrototypes{"jink", "slash", "peach", "fire_slash", "thunder_slash",
                "archery_attack", "savage_assault", "duel", "analeptic"};
            if (!names.contains(name) && !commonPrototypes.contains(name)
                && !request.worldView.strategyFacts.value("pattern_class_names").toObject().contains(name))
                return failure("printed evaluation name is outside the public pattern catalog");
        }
        const bool treasurePolicy = skillName == "_zhaogujing";
        const bool shuangrenPolicy = skillName == "shuangren";
        const bool yonglvePolicy = skillName == "_yonglve";
        const bool qicePolicy = skillName == "qice";
        const bool olGuhuoPolicy = skillName == "olguhuo";
        const bool olChangbiaoPolicy = skillName == "olchangbiao";
        if ((olGuhuoPolicy || olChangbiaoPolicy) && (request.kind != AIRequest::Activate
            || request.reason != CardUseStruct::CARD_USE_REASON_PLAY))
            return failure("OL hypothetical declaration is turn planning only");
        const bool huomoPolicy = skillName == "huomo";
        const bool arkGuozaiPolicy = skillName == "ark_guozai";
        if (arkGuozaiPolicy && request.kind != AIRequest::Activate)
            return failure("Arknights Guozai evaluation outside turn planning");
        const bool elucidatorPolicy = skillName == "Se_Elucidator";
        const bool koujingPolicy = skillName == "koujing";
        const bool dragonWushengPolicy = skillName == "Dragon_wusheng";
        if (dragonWushengPolicy && request.kind != AIRequest::Activate)
            return failure("Dragon Wusheng evaluation outside turn planning");
        const bool shujianPolicy = skillName == "_shujian";
        if (elucidatorPolicy) {
            const QJsonObject effect = request.choiceOptions.context.value("effect").toObject();
            ServerPlayer *target = m_room.findPlayerByObjectName(effect.value("to").toString());
            const QJsonArray kinds = effect.value("card").toObject().value("kind_of").toArray();
            if (request.kind != AIRequest::SkillInvoke || request.choiceOptions.reason != skillName
                || !request.choiceOptions.context.value("effect_present").toBool()
                || effect.value("from").toString() != player->objectName()
                || !target || !target->isAlive() || m_room.getCurrent() != player
                || !player->hasWeapon(skillName) || !kinds.contains("Slash"))
                return failure("Elucidator evaluation outside its owner Slash effect");
        }
        if (koujingPolicy && (request.kind != AIRequest::Discard || request.choiceOptions.reason != "koujing"))
            return failure("Koujing evaluation outside its discard question");
        if (shujianPolicy && (request.kind != AIRequest::UseCard || request.pattern != "@@shujian"
            || !player->hasSkill("shujian")))
            return failure("Shujian evaluation outside its response-use question");
        const bool rushShensu = skillName == "shensu" && request.kind == AIRequest::UseCard
            && (request.pattern == "@@rushB_baobian_shensu1" || request.pattern == "@@rushB_baobian_shensu2")
            && player->hasSkill("rushB_baobian");
        const bool fumianPolicy = skillName == "fumian";
        if (fumianPolicy && (request.kind != AIRequest::Choice || request.choiceOptions.reason != "fumian"))
            return failure("Fumian evaluation outside original choice");
        const bool declared2016 = skillName == "jiaozhao" || skillName == "taoluan" || skillName == "tenyeartaoluan";
        const bool lihuoMalformedPolicy = skillName == "lihuo";
        if (lihuoMalformedPolicy && request.kind != AIRequest::Activate)
            return failure("Lihuo malformed source evaluation is play-only");
        if (yonglvePolicy && (request.kind != AIRequest::SkillInvoke || request.choiceOptions.reason != "yonglve"
            || !player->hasSkill("yonglve"))) return failure("Yonglve evaluation prompt is not active");
        if (shuangrenPolicy && (request.kind != AIRequest::PlayerChosen || request.choiceOptions.reason != "shuangren"))
            return failure("Shuangren planning prompt is not active");
        if (treasurePolicy) {
            const QString pattern = "BasicCard,TrickCard+^DelayedTrick|.|.|hand";
            if (request.kind != AIRequest::RespondCard || request.choiceOptions.question != "askForCard"
                || request.choiceOptions.reason != pattern || request.pattern != pattern
                || request.prompt != "_zhaogujing0:" || request.handlingMethod != Card::MethodNone
                || !player->hasTreasure("_zhaogujing")) return failure("treasure planning prompt is not active");
            bool disclosed = false;
            for (const AICardView &known : request.worldView.handCards) {
                if (known.objectName != name || !player->handCards().contains(known.effectiveId)) continue;
                const Card *material = Sanguosha->getCard(known.effectiveId);
                if (material && material->objectName() == name && (material->isKindOf("BasicCard") || material->isNDTrick())) disclosed = true;
            }
            if (!disclosed) return failure("treasure planning name is not viewer hand");
        }
        if (proposal.size() != 4 || !proposal.value("subcards").isArray()
            || (skillName != "guhuo" && skillName != "nosguhuo" && skillName != "shensu" && skillName != "jixi"
                && skillName != "guose" && skillName != "wusheng" && skillName != "gdlonghun" && skillName != "jinxianwan" && !shuangrenPolicy && !treasurePolicy && !yonglvePolicy && !qicePolicy && !huomoPolicy && !lihuoMalformedPolicy && !printedPolicy && !declared2016 && !fumianPolicy && !elucidatorPolicy && !koujingPolicy && !shujianPolicy && !arkGuozaiPolicy && !dragonWushengPolicy && !nyarzPolicy && !tousuiPolicy && !erangwaiPolicy && !wuxingPolicy && !faqiPolicy && !nyBeautyPolicy && !fenyuePolicy && !olGuhuoPolicy && !olChangbiaoPolicy && !taozeiTmPolicy && !chouliePolicy && !xixiangPolicy && !panqinPolicy && !renchouPolicy && !r50Policy && !r52OlspPolicy) || (!wuxingPolicy && !treasurePolicy && !yonglvePolicy && !printedPolicy && !shujianPolicy && !fenyuePolicy && !rushShensu && !chouliePolicy && !renchouPolicy && !r50Policy && !r52OlspPolicy && !player->hasSkill(skillName)))
            return failure("skill evaluation is not an audited owner policy");
        const QJsonArray costs = proposal.value("subcards").toArray();
        if ((olGuhuoPolicy && costs.size() != 1)
            || (olChangbiaoPolicy && name != "slash")
            || (nyBeautyPolicy && (nyTongli ? costs.size()>1 : !costs.isEmpty()))
            || ((tousuiPolicy || erangwaiPolicy) && (name != "slash" || !costs.isEmpty()))
            || (faqiPolicy && !costs.isEmpty())
            || (wuxingPolicy && costs.size() != 1)
            || (declared2016 && costs.size() != 1) || ((treasurePolicy || huomoPolicy || printedPolicy || fumianPolicy || nyarzPolicy) && !costs.isEmpty())
            || (arkGuozaiPolicy && (name != "slash" || !costs.isEmpty()))
            || (elucidatorPolicy && (name != "duel" || !costs.isEmpty()))
            || ((koujingPolicy || dragonWushengPolicy) && (name != "slash" || costs.size() != 1))
            || (shujianPolicy && (name != "dismantlement" || !costs.isEmpty()))
            || (lihuoMalformedPolicy && (name != "fire_slash" || !costs.isEmpty()))
            || ((skillName == "shensu" || skillName == "jinxianwan" || shuangrenPolicy || yonglvePolicy) && (name != "slash" || !costs.isEmpty()))
            || ((skillName == "guhuo" || skillName == "nosguhuo") && costs.size() > 1)
            || (skillName == "jixi" && (name != "snatch" || costs.size() != 1))
            || (skillName == "guose" && (name != "indulgence" || costs.size() != 1))
            || (skillName == "wusheng" && (name != "slash" || costs.size() != 1))
            || (skillName == "gdlonghun" && (name != "fire_slash" || costs.size() != 1))) return failure("invalid skill evaluation shape");
        EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
        LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
        ResponseNativeScope responseScope(m_room, player, request.reason, request.pattern);
        std::unique_ptr<Card> card(Sanguosha->cloneCard(name,
            (arkGuozaiPolicy || nyBeautyPolicy || renchouLimit || r50Policy) ? Card::SuitToBeDecided : Card::NoSuit, (arkGuozaiPolicy || erangwaiPolicy || nyBeautyPolicy || renchouLimit || r50Policy) ? -1 : 0));
        if (!card && (printedPolicy || nyarzPolicy)) return {{"exists",false}};
        if (!card || ((skillName == "guhuo" || skillName == "nosguhuo" || olGuhuoPolicy) && !(card->isKindOf("BasicCard") || card->isNDTrick())))
            return failure("invalid skill evaluation card class");
        if (((declared2016 || fumianPolicy) && !(card->isKindOf("BasicCard") || card->isNDTrick()))
            || (qicePolicy && (!card->isNDTrick() || costs.isEmpty()))
            || (huomoPolicy && !card->isKindOf("BasicCard"))) return failure("invalid declared evaluation class");
        if (wuxingPolicy && (!card || !card->isKindOf("NatureSlash"))) return failure("Wuxing hypothesis is not a NatureSlash");
        if (qicePolicy) {
            QList<int> proposed;
            for (const auto &value : costs) proposed << value.toInt(-1);
            if (proposed != player->handCards()) return failure("Qice evaluation must preserve the whole hand order");
        }
        for (const QJsonValue &value : costs) {
            const double number = value.toDouble(-1);
            if (!value.isDouble() || number < 0 || number > std::numeric_limits<int>::max()
                || number != std::floor(number)) return failure("invalid skill evaluation cost");
            QList<int> payable = skillName == "jixi" ? player->getPile("field") : player->handCards();
            if (panqinPolicy) for (const auto &v : request.worldView.discardPile) payable << v.effectiveId;
            if (declared2016 || xixiangPolicy || r50Policy || dragonWushengPolicy || skillName == "guose" || skillName == "wusheng" || skillName == "gdlonghun")
                for (const Card *equip : player->getEquips()) payable << equip->getEffectiveId();
            if (declared2016 || wuxingPolicy) payable.append(player->getHandPile());
            if (!payable.contains(int(number))) return failure("skill evaluation cost is outside owner policy");
            bool disclosed = false;
            if (panqinPolicy) for (const auto &v : request.worldView.discardPile)
                if (v.effectiveId == int(number)) disclosed = true;
            for (const AICardView &known : request.worldView.handCards)
                if (known.effectiveId == int(number)) disclosed = true;
            if (declared2016 || xixiangPolicy || r50Policy || dragonWushengPolicy || skillName == "guose" || skillName == "wusheng" || skillName == "gdlonghun")
                for (const AICardView &known : request.worldView.self.equips)
                    if (known.effectiveId == int(number)) disclosed = true;
            if (skillName == "jixi")
                for (const AICardPileView &pile : request.worldView.self.piles)
                    if (pile.name == "field" && pile.cardIds.contains(int(number))) disclosed = true;
            if ((declared2016 || wuxingPolicy) && player->getHandPile().contains(int(number)))
                for (const AICardPileView &pile : request.worldView.self.piles)
                    for (const AICardView &known : pile.cards)
                        if (known.effectiveId == int(number)) disclosed = true;
            if (!disclosed) return failure("skill evaluation cost was not projected");
            if (wuxingPolicy && (!Sanguosha->getCard(int(number))->isKindOf("NatureSlash")
                || Sanguosha->getCard(int(number))->objectName() == name)) return failure("Wuxing source requires a different elemental Slash");
            card->addSubcard(int(number));
        }
        if (skillName != "guose" && !shuangrenPolicy && !printedPolicy && !fumianPolicy && !chouliePolicy && !renchouLimit) card->setSkillName(skillName);
        if (skillName == "nyarz_zhishi") card->setSkillName("_nyarz_zhishi_card");
        if (skillName == "nymansi") card->setSkillName("_nymansi");
        if (skillName == "ny_zhenge") card->setSkillName("_ny_zhenge");
        if (skillName == "nyarz_zhifou") card->setSkillName(QString());
        if (fumianPolicy) card->setFlags("fumian_distance");
        if (chouliePolicy && !configureAIRound48ChoulieEvaluation(*card))
            return failure("Choulie readonly dummy differs from the fixed source");
        std::unique_ptr<AITianyiQueryScope> noDistance;
        if (shuangrenPolicy) noDistance.reset(new AITianyiQueryScope(player, nullptr));
        int budget = 4096;
        AICardCandidateView candidate = makeAICardCandidate(m_room, player, card.get(),
            Card::MethodUse, AIRequest::Activate, QString(), budget);
        const int opaqueId = -3000000 - int(request.conversionLedger->queryCount);
        candidate.cardId = opaqueId;
        QJsonObject row = publicDecisionCard(card.get(), true);
        row.insert("evaluation_only", true); row.insert("virtual_card", true);
        row.insert("effective_id", costs.isEmpty() ? opaqueId : costs.first().toInt()); row.insert("id", opaqueId);
        row.insert("subcards", costs); row.insert("can_recast", card->canRecast());
        row.insert("legacy_string_key", card->toString());
        row.insert("legacy_sort_key", legacySortKey(card.get()));
        row.insert("locked", player->isLocked(card.get())); row.insert("using", card->hasFlag("using"));
        row.insert("target_fixed", card->targetFixed()); row.insert("handling_method", int(card->getHandlingMethod()));
        row.insert("target_residue", responseResidueJson(responseResidue(m_room, player, card.get())));
        row.insert("evaluation_rules", responseOwnerRules(m_room, player, card.get()));
        row.insert("evaluation_owner", player->objectName());
        const QJsonObject targetData = responseCandidateJson(candidate);
        for (auto it = targetData.begin(); it != targetData.end(); ++it) row.insert(it.key(), it.value());
        if (!aiRound44RangeRequestCurrent(m_room,parentRequest))
            return failure("skill evaluation changed game state");
        if (r50Policy) row.insert("round50_source",skillName);
        if (!aiRound44RangeRequestCurrent(m_room,parentRequest)) return failure("R50 source query changed native state");
        const auto recipeKey = m_room.roomRuntime()->ai().issueReadonlyCard(parentRequest, player, std::move(card), skillName);
        if (!recipeKey.isEmpty()) row.insert("readonly_recipe_key", recipeKey);
        return {{"card", row}, {"candidate", targetData}};
    }
    const QString className = proposal.value("class_name").toString();
    const int scopeId = proposal.value("scope_id").toInt(-1);
    const QJsonObject scope = request.conversionPolicy.value("scopes").toObject()
        .value(QString::number(scopeId)).toObject();
    if (scope.isEmpty() || scope.value("class_name").toString() != className)
        return failure("conversion policy scope was not issued for this class");
    const bool skillAction = proposal.value("kind").toString() == "skill_action";
    if (skillAction != scope.value("skill_action").toBool())
        return failure("skill action scope kind mismatch");
    const QJsonObject spec = proposal.value("spec").toObject();
    const bool selectedMaterialProposal = proposal.contains("selected_materials");
    QList<int> selectedMaterials;
    if (selectedMaterialProposal) {
        if (skillAction || !proposal.value("selected_materials").isArray())
            return failure("selected-material proposal shape is invalid");
        for (const auto &value : proposal.value("selected_materials").toArray()) {
            const double n = value.toDouble(-1);
            if (!value.isDouble() || n < 0 || n != std::floor(n)
                || n > std::numeric_limits<int>::max() || selectedMaterials.contains(int(n)))
                return failure("selected-material identity is invalid");
            selectedMaterials << int(n);
        }
        const QJsonObject issuedIdentity = scope.value("identity").toObject();
        const bool chumingGenerated = selectedMaterials.isEmpty()
            && authorizeAIRound24ChumingPayment(request, player,
                responseRef(issuedIdentity, "activation"), responseRef(issuedIdentity, "source"), spec);
        if (!chumingGenerated && !aiAuditedNonconsumingSelection(request, player, scope.value("skill_name").toString(),
                spec, responseIds(selectedMaterials)))
            return failure("selected-material constructor is outside audited source");
    }
    const bool legacyResponse = spec.contains("legacy_class");
    std::unique_ptr<SkillCard> legacyPrototype;
    QString legacyPrototypeName;
    if (legacyResponse) {
        static const QMap<QString, QString> responsePrototypes{
            {"ejisi", "#ejisiCard"},
            {"lol_ayjm_r", "#lol_ayjm_rCard"},
            {"lol_bjfh_e", "#lol_bjfh_eCard"},
            {"lol_bjfh_q", "#lol_bjfh_qCard"},
            {"lol_bjfh_r", "#lol_bjfh_rCard"},
            {"lol_bzll_e", "#lol_bzll_eCard"},
            {"lol_hbss_e", "#lol_hbss_eCard"},
            {"lol_hbss_q", "#lol_hbss_qCard"},
            {"lol_jwyh_e", "#lol_jwyh_eCard"},
            {"lol_jwyh_q", "#lol_jwyh_qCard"},
            {"lol_jwyh_r", "#lol_jwyh_rCard"},
            {"lol_jwyh_w", "#lol_jwyh_wCard"},
            {"lol_mzzw_q", "#lol_mzzw_qCard"},
            {"lol_mzzw_r", "#lol_mzzw_rCard"},
            {"lol_mzzw_w", "#lol_mzzw_wCard"},
            {"lol_smss_e", "#lol_smss_eCard"},
            {"lol_smss_q", "#lol_smss_qCard"},
            {"lol_smss_r", "#lol_smss_rCard"},
            {"lol_smss_w", "#lol_smss_wCard"},
            {"lol_sxls_e", "#lol_sxls_eCard"},
            {"lol_sxls_r", "#lol_sxls_rCard"},
            {"lol_sxls_w", "#lol_sxls_wCard"},
            {"lol_tqz_e", "#lol_tqz_eCard"},
            {"lol_tqz_r", "#lol_tqz_rCard"},
            {"lol_tqz_w", "#lol_tqz_wCard"},
            {"lol_xlnw_e", "#lol_xlnw_eCard"},
            {"lol_xlnw_r", "#lol_xlnw_rCard"},
            {"lol_xlnw_w", "#lol_xlnw_wCard"},
            {"PlusYiji", "#PlusYiji_Card"},
            {"fatewangzhe", "#fatewangzhe"},
            {"fateqiuzhan", "#fateqiuzhan_card"},
            {"fateshenjian", "#fateshenjian_card"},
            {"fatebumo", "#fatebumo_card"},
            {"fateyaoshu", "#fateyaoshu"},
            {"fatechuyi_vs", "#fatechuyi_card"},
            {"fatetiangong_vs", "#fatetiangong_card"},
            {"fateloli_vs", "#fateloli_card"},
            {"fatefanshi", "#fatefanshi_card"},
            {"fateheijian", "#fateheijian_card"},
            {"fatexinyin", "#fatexinyin_card"},
            {"fatemoshu_vs", "#fatemoshu_card"},
            {"fatejuntuan_vs", "#fatejuntuan_card"},
            {"fatesiji_vs", "#fatesiji_card"},
            {"fatechongshu", "#fatechongshu_card"},
            {"sy_yaohuo", "#sy_yaohuoCard"},
            {"sy_zuijiu", "#sy_zuijiuCard"},
            {"sy_zuijiuNormalSlash", "#sy_zuijiuNormalSlashCard"},
            {"sy_luansi", "#sy_luansiCard"},
            {"sy_dihui", "#sy_dihuiCard"},
            {"sy_chanxian", "#sy_chanxianCard"},
            {"sy_zhiheng", "#sy_zhiheng"},
            {"sy_zhongzhang", "#sy_zhongzhang"},
            {"sy_baofeng", "#sy_baofengCard"},
            {"sy_longnu", "#sy_longnuCard"},
            {"sy_shishen", "#sy_shishenCard"},
            {"nyyuqi", "#nyyuqi"},
            {"nymiaojian", "#nymiaojian"},
            {"nyguose", "#nyguose"},
            {"nylijian", "#nylijian"},
            {"nyxuechang", "#nyxuechang"},
            {"nybeifen", "#nybeifen"},
            {"nyyuanyu", "#nyyuanyu"},
            {"nyxiyan", "#nyxiyan"},
            {"nywuniang", "#nywuniang"},
            {"ny_qiaoshi", "#ny_qiaoshi"},
            {"ny_yanyu", "#ny_yanyu"},
            {"ny_luochong", "#ny_luochong"},
            {"nyarz_caiyi", "#nyarz_caiyi"},
            {"nyarz_guangu", "#nyarz_guangu"},
            {"nyarz_zhifou", "#nyarz_zhifou"},
            {"nyarz_anzhi", "#nyarz_anzhi"},
            {"nyarz_yaoyi", "#nyarz_yaoyi"},
            {"bifa", "BifaCard"}, {"songci", "SongciCard"},
            {"xueji", "XuejiCard"},
            {"neoluoyi", "LuoyiCard"},
            {"neofanjian", "NeoFanjianCard"},
            {"kuangxi", "KuangxiCard"}, {"yl_diting", "#yl_ditingCard"},
            {"tieba_zhili", "#tieba_zhiliCard"}, {"tiaojiaoCMT", "#tiaojiaoCMTCard"},
            {"ms_mimou", "#ms_mimou"}, {"ms_yongheng", "#ms_yonghengCard"}, {"ms_guimou", "#ms_guimou"},
            {"se_zhifu", "#se_zhifucard"}, {"se_nike", "#se_nikecard"},
            {"LuaFengxi", "#LuaFengxiCard"}, {"LuaLinlu", "#LuaLinluCard"},
            {"LuaDuanjian", "#LuaDuanjianCard"}, {"LuaHuowei", "#LuaHuoweiCard"},
            {"tongyiTQ", "#tongyiTQ"},
            {"kehuisuan", "#kehuisuanCard"},
            {"tiaojiao", "TiaojiaoCard"},
            {"sitian", "SitianCard"},
            {"lisao", "LisaoCard"},
            {"qixin", "QixinCard"},
            {"luaRsihu", "#luaRsihuCard"},
            {"luaRziao", "#luaRziao"},
            {"luaRzhangshi", "#luaRzhangshicard"},
            {"yj_xinge", "#yj_xingecard"},
            {"yj_zhengyu&", "#yj_zhengyuCard"},
            {"zd_fenyue", "#zd_fenyueCard"},
            {"jiwu", "JiwuCard"},
            {"EXCard_WLJ", "#EXCard_WLJ_SkillCARD"},
            {"juao", "JuaoCard"},
            {"houyuan", "HouyuanCard"},
            {"shouye", "ShouyeCard"},
            {"jueyan", "JueyanCard"},
            {"xiongluan", "XiongluanCard"},
            {"poxi", "PoxiCard"},
            {"huairou", "HuairouCard"},
            {"qingce", "QingceCard"},
            {"olqingce", "OLQingceCard"},
            {"sy_old_bolue", "#sy_old_bolueCard"},
            {"sy_old_yaohuo", "#sy_old_yaohuoCard"},
            {"sy_old_sanzhi", "#sy_old_sanzhiCard"},
            {"sy_old_luansi", "#sy_old_luansiCard"},
            {"sy_old_dihui", "#sy_old_dihuiCard"},
            {"sy_old_chanxian", "#sy_old_chanxianCard"},
            {"jinchoufa", "JinChoufaCard"},
            {"jinyanxi", "JinYanxiCard"},
            {"jinsanchen", "JinSanchenCard"},
            {"jinxiongzhi", "JinXiongzhiCard"},
            {"blood_hj", "#blood_hj"},
            {"zhiyuan", "ZhiyuanCard"},
            {"flood", "FloodCard"},
            {"zhongyi", "ZhongyiCard"},
            {"taichen_fight", "TaichenFightCard"},
            {"tousui", "TousuiCard"},
            {"chexuan", "ChexuanCard"},
            {"caozhao", "CaozhaoCard"},
            {"ckdianji", "#ckdianjicard"},
            {"ckluolei", "#ckluoleicard"},
            {"cksilie", "#cksiliecard"},
            {"cklongxi", "#cklongxicard"},
            {"ckweishan", "#ckweishancard"},
            {"sy_quanqing", "#sy_quanqingCard"},
            {"sy_ejue", "#sy_ejueCard"},
            {"fcmk_jlsg_meihuo", "#fcmk_jlsg_meihuoCard"},
            {"feijun", "FeijunCard"},
            {"shenshi", "ShenshiCard"},
            {"chenglve", "ChenglveCard"},
            {"zhenliang", "ZhenliangCard"},
            {"olzhenliang", "OLZhenliangCard"},
            {"zhanhuo", "ZhanhuoCard"},
            {"se_chicheng", "#se_chichengcard"},
            {"se_hongzha", "#se_hongzha"},
            {"fanqian", "#fanqian"},
            {"eryu", "#eryu"},
            {"nuequ", "#nuequ"},
            {"keshilun", "#keshilun"},
            {"jinshidu", "JinShiduCard"},
            {"jinruilve_give", "JinRuilveGiveCard"},
            {"efusha", "#efusha"},
            {"eyanshou", "#eyanshouCard"},
            {"etiaobo", "#etiaoboCard"},
            {"shenjiZHCMT", "#shenjiZHCMTCard"},
            {"zhuiluZHCMT", "#zhuiluZHCMTCard"},
            {"XiansiZHCMT", "#XiansiZHCMTCard"},
            {"XiansiZHCMTSlash", "#XiansiZHCMTSlashCard"},
            {"zhengsiZHCMT", "#zhengsiZHCMTCard"},
            {"Dragon_rende", "#Dragon_rendeCard"},
            {"Dragon_feijiang", "#Dragon_feijiangCard"},
            {"Dragon_fuyuanVS", "#Dragon_fuyuanCard"},
            {"tongling", "#tonglingCard"},
            {"Dragon_kurou", "#Dragon_kurouCard"},
            {"Dragon_jiedao", "#Dragon_jiedaoCard"},
            {"Dragon_qingnang", "#Dragon_qingnangCard"},
            {"Promote_zhiheng", "#Promote_zhihengCard"},
            {"Promote_fanjian", "#Promote_fanjianCard"},
            {"Promote_qingnang", "#Promote_qingnangCard"},
{"jijiang", "JijiangCard"},
            {"guhuo", "GuhuoCard"}, {"aocai", "AocaiCard"}, {"heg_aocai", "HAocaiCard"}, {"huayuan", "#huayuanCard"},
            {"LuaFanjian", "#LuaFanjianCard"}, {"LuaXinyiji", "#LuaXinyiji"}, {"LuaJuece", "#LuaJuece"},
            {"zuguangu", "#zuguanguCard"},
            {"zuyirong", "#zuyirongCard"},
            {"kezuyunshen", "#kezuyunshenCard"},
            {"kezulieshi", "#kezulieshiCard"},
            {"kezubolong", "#kezubolongCard"},
            {"zukaiji", "#zukaijiCard"},
            {"zutanfeng", "#zutanfengCard"},
            {"zulianzhuvs", "#zulianzhuCard"},
            {"kezufuxun", "#kezufuxunCard"},
            {"kezuxumin", "#kezuxuminCard"},
            {"kezumingjie", "#kezumingjieCard"},
            {"kezuqiuxin", "#kezuqiuxinCard"},
            {"biejun-give", "BiejunCard"}, {"shujian", "ShujianCard"},
            {"fujue", "FujueCard"}, {"beiyu", "BeiyuCard"},
            {"thqimei", "ThQimeiCard"}, {"gongqiao", "GongqiaoCard"},
            {"nosjiefan", "NosJiefanCard"}, {"nosrenxin", "NosRenxinCard"},
            {"kerenxin", "#kerenxinCard"}, {"erangwai", "#erangwaiCard"},
            {"feiyang", "FeiyangCard"},
            {"haremu", "HaremuCard"}, {"youshui", "YoushuiCard"}, {"hayate_zhufu", "ZhufuCard"},
            {"luaRshiji", "#luaRshiji"},
            {"bawang", "BawangCard"}, {"fuzuo", "FuzuoCard"},
            {"luaxiongfeng", "#luaxiongfeng"},
            {"dujiang", "DujiangCard"},
            {"smalltuxi", "SmallTuxiCard"},
            {"hongyuan", "HongyuanCard"},
            {"zhenwei", "ZhenweiCard"},
            {"jiuzhu", "JiuzhuCard"},
            {"ckbaihe", "#ckbaihecard"},
            {"ckshiliang", "#ckshiliangcard"},
            {"ckjinjie", "#ckjinjieCard"},
            {"kuizhu", "KuizhuCard"},
            {"se_zhikong", "#se_zhikong"},
            {"se_weishi", "#se_weishi"},
            {"jinxianwan", "JinXianwanCard"}, {"jinxuanbei", "JinXuanbeiCard"}, {"shouji", "#shoujicard"}, {"jisha", "#jishaCard"}, {"Jiuzi", "#JiuziCard"}, {"ov_muyue", "#ov_muyueCard"},
            {"gongqi", "GongqiCard"}, {"jiefan", "JiefanCard"}, {"anxu", "AnxuCard"}, {"qice", "QiceCard"},
            {"qinwang", "QinwangCard"}, {"zhenshan", "ZhenshanCard"}, {"huomo", "HuomoCard"},
            {"chunlao", "ChunlaoCard"}, {"olchunlao", "OLChunlaoCard"}, {"qiaoshui", "QiaoshuiCard"}, {"huimin", "HuiminCard"}, {"tongbo", "TongboCard"},
            {"zhongjian", "ZhongjianCard"}, {"olzhongjian", "OLZhongjianCard"},
            {"mobileqingxian", "MobileQingxianCard"}, {"mobilecanyun", "MobileCanyunCard"},
            {"wengua", "WenguaCard"}, {"wenguagive", "WenguagiveCard"}, {"qinqing", "QinqingCard"}, {"huisheng", "HuishengCard"}, {"jishe", "JisheCard"},
            {"xiansi", "XiansiCard"}, {"xiansi_slash", "XiansiSlashCard"},
            {"mingjian", "MingjianCard"}, {"huaiyi", "HuaiyiCard"}, {"yanzhu", "YanzhuCard"},
            {"xingxue", "XingxueCard"}, {"yjyanyu", "YjYanyuCard"}, {"wurong", "WurongCard"},
            {"anguo", "AnguoCard"}, {"olzhaofu", "OLzhaofuCard"}};
        const QString skillName = scope.value("skill_name").toString();
        int auditedPrototype = aiAuditedSourcePrototype(skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiAuditedConditionalSourcePrototype(request, skillName, spec.value("legacy_class").toString(), player);
        if (auditedPrototype < 0)
            auditedPrototype = aiAuditedConstructionSourcePrototype(request, player, skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound21ReadonlyPrototype(skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound21PlanningPrototype(request, player, skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound21ChoicePrototype(request, player, skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound43InovationPrototype(request, player, skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound45InovationPrototype(request, player, skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound47SijyuPrototype(request, player, skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound51JijiaoPrototype(request, player, skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound22OlClanPrototype(request, player, skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound22OlClanActionPrototype(skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound22ChoicePrototype(request, player, skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound23ChoicePrototype(request, player, skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound24FeiDIYPrototype(request, player, skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound42PlanningPrototype(request, player, skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound24OriginalPrototype(request, player, skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound25ChoicePrototype(request, player, skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound42ChoicePrototype(request, player, skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound43ChoicePrototype(request, player, skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound44SkinPrototype(request, player, skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound45NewgeneralsPrototype(request,player,skillName,spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound46PlanningPrototype(request,player,skillName,spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound49qRenchouPrototype(request,player,skillName,spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound50PlanningPrototype(request,player,skillName,spec.value("legacy_class").toString());
        if (skillName == "mtjieli" && !aiRound43ChoiceDeclaration(request, player, spec))
            return failure("Mtjieli selected proxy requires zero costs and red/black declaration");
        if (auditedPrototype < 0)
            auditedPrototype = aiRound26ChoicePrototype(request, player, skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound27ChoicePrototype(request, player, skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound25ResponsePrototype(request, player, skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound42ResponsePrototype(request, player, skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound44LeiPrototype(request, player, skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound25OriginalPrototype(request, player, skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound26OriginalPrototype(request, player, skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound27OriginalPrototype(request, player, skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound36KurouPrototype(request, player, skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound29ExtraPrototype(request, player, skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound26DelegatePrototype(request, player, skillName, spec.value("legacy_class").toString());
        if (auditedPrototype < 0)
            auditedPrototype = aiRound22OlClanObservationPrototype(request, player, skillName, spec.value("legacy_class").toString());
        const bool declared = (auditedPrototype >= 0 && (auditedPrototype & 2) && spec.contains("user_string"))
            || skillName == "caozhao" || skillName == "guhuo" || skillName == "aocai"
            || skillName == "jinxianwan" || skillName == "qice" || skillName == "qinwang"
            || skillName == "zhenshan" || skillName == "huomo" || skillName == "tongyiTQ";
        if ((skillName == "jinxuanbei" || skillName == "shouji") && (!skillAction || request.kind != AIRequest::Activate
            || request.reason != CardUseStruct::CARD_USE_REASON_PLAY))
            return failure("JinXuanbei prototype is restricted to its play action");
        legacyPrototypeName = auditedPrototype >= 0 ? spec.value("legacy_class").toString()
            : responsePrototypes.value(skillName);
        if (request.kind == AIRequest::UseCard && skillName == "nyarz_guangu"
            && request.pattern == "@@nyarz_guangu" && spec.value("legacy_class") == "#nyarz_guangu_use")
            legacyPrototypeName = "#nyarz_guangu_use";
        if (request.kind == AIRequest::UseCard && skillName == "nyarz_anzhi"
            && request.pattern == "@@nyarz_anzhi"
            && (spec.value("legacy_class") == "#nyarz_anzhi_dis" || spec.value("legacy_class") == "#nyarz_anzhi_give"))
            legacyPrototypeName = spec.value("legacy_class").toString();
        if (skillName == "ejisi" && (skillAction || className != "Nullification"
            || (request.kind != AIRequest::RespondCard && request.kind != AIRequest::UseCard)
            || (request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE
                && request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
            || request.pattern != "nullification"))
            return failure("Ejisi prototype is restricted to its native nullification response");
        if (skillName == "huayuan" && (request.kind != AIRequest::UseCard || request.pattern != "@@huayuan"))
            return failure("Huayuan prototype outside its response-use question");
        if (skillName == "chunlao") {
            if (request.pattern == "@@chunlao") legacyPrototypeName = "ChunlaoCard";
            else if (request.pattern == "peach" || request.pattern == "peach+analeptic") legacyPrototypeName = "ChunlaoWineCard";
            else return failure("Chunlao prototype outside its prompt");
        }
        if (skillName == "qiaoshui" && request.pattern == "@@qiaoshui!") legacyPrototypeName = "ExtraCollateralCard";
        if (skillName == "huaiyi" && request.pattern == "@@huaiyi") legacyPrototypeName = "HuaiyiSnatchCard";
        if (skillName == "xingxue" && request.pattern != "@@xingxue") return failure("Xingxue prototype outside its prompt");
        static const QSet<QString> paidPrototypes{"PlusYiji", "lol_bjfh_e", "lol_bjfh_q", "lol_bjfh_r", "lol_bzll_e", "lol_hbss_e", "lol_hbss_q", "lol_jwyh_e", "lol_jwyh_q", "lol_jwyh_r", "lol_smss_e", "lol_smss_q", "lol_smss_r", "lol_smss_w", "lol_sxls_w", "lol_tqz_e", "lol_tqz_w", "lol_xlnw_r", "lol_xlnw_w", "fatechongshu", "fatechuyi_vs", "fateheijian", "fatejuntuan_vs", "fatemoshu_vs", "fateshenjian", "fatesiji_vs", "fatetiangong_vs", "fatewangzhe", "fateyaoshu", "ny_qiaoshi", "ny_yanyu", "nyarz_anzhi", "nyarz_caiyi", "nyarz_guangu", "nybeifen", "nyxiyan", "sy_baofeng", "sy_longnu", "sy_shishen", "sy_zhiheng", "feiyang", "sitian", "tiaojiao", "haremu", "youshui", "luaRsihu", "yj_xinge", "yj_zhengyu&", "zd_fenyue", "jiwu", "juao", "houyuan", "shouye", "fuzuo", "huairou", "qingce", "olqingce", "sy_old_sanzhi", "sy_old_chanxian", "zhiyuan", "flood", "zhongyi", "tousui", "chexuan", "caozhao", "dujiang", "jiuzhu", "ckdianji", "cksilie", "ckweishan", "sy_quanqing", "sy_ejue", "fcmk_jlsg_meihuo", "feijun", "shenshi", "zhenliang", "olzhenliang", "se_chicheng", "se_hongzha", "nuequ", "se_zhikong", "se_weishi", "jinruilve_give", "efusha", "erangwai", "LuaLinlu", "LuaDuanjian", "LuaHuowei", "ms_mimou", "ms_guimou", "se_zhifu", "yl_diting", "tiaojiaoCMT", "bifa", "xueji", "neoluoyi", "neofanjian", "Dragon_rende", "Dragon_feijiang", "Dragon_fuyuanVS", "Dragon_jiedao", "Dragon_qingnang", "Promote_zhiheng", "Promote_fanjian", "Promote_qingnang", "zulianzhuvs", "kezufuxun", "kezuxumin", "kezumingjie", "kezuqiuxin", "LuaFanjian", "LuaXinyiji", "LuaJuece", "biejun-give", "shujian", "gongqiao", "thqimei", "guhuo", "ov_muyue", "huimin", "tongbo", "zhongjian", "olzhongjian", "mobileqingxian", "mobilecanyun", "wengua", "wenguagive", "qinqing", "huisheng", "jishe", "shouji", "gongqi", "qice", "qinwang", "huomo", "chunlao", "olchunlao", "xiansi_slash", "yjyanyu", "wurong"};
        if (legacyPrototypeName.isEmpty() || spec.value("legacy_class").toString() != legacyPrototypeName
            || (!skillAction && spec.value("skill").toString() != skillName)
            || spec.size() != (skillAction ? 2 : 3) + (declared ? 1 : 0)
            || !spec.value("subcards").isArray()
            || (!paidPrototypes.contains(skillName) && !(auditedPrototype >= 0 && (auditedPrototype & 1))
                && !spec.value("subcards").toArray().isEmpty()))
            return failure("legacy response prototype does not match issued source");
        if (declared) {
            const QString declaration = spec.value("user_string").toString();
            // JinXianwan's donor declares the literal Slash/Jink family name.
            // patterns(true) may map Slash to a same-class printed variant;
            // that catalogue alias must not change this provider's declaration.
            const QString expected = skillAction
                ? (request.reason == CardUseStruct::CARD_USE_REASON_PLAY ? QStringLiteral("slash") : request.pattern)
                : skillName == "jinxianwan" && className == "Slash" ? QStringLiteral("slash")
                : skillName == "jinxianwan" && className == "Jink" ? QStringLiteral("jink")
                : scope.value("card_name").toString();
            if (skillName == "jinxianwan" && ((declaration != "slash" && declaration != "jink") || declaration != expected))
                return failure("legacy declaration does not match current native scope");
            if (declaration.isEmpty()) return failure("legacy declared prototype needs a name");
        } else if (spec.contains("user_string")) return failure("legacy response cannot declare a card name");
    }
    static const QSet<QString> allowed{"name", "suit", "number", "skill", "subcards", "user_string", "legacy_class"};
    for (auto it = spec.begin(); it != spec.end(); ++it)
        if (!allowed.contains(it.key())) return failure("unknown conversion proposal identity field");
    if (skillAction) for (auto it = spec.begin(); it != spec.end(); ++it)
        if (it.key() != "subcards" && it.key() != "user_string" && !(legacyResponse && it.key() == "legacy_class"))
            return failure("skill action caller cannot prescribe native printed identity");
    if ((!skillAction && !legacyResponse && (!spec.value("name").isString() || !spec.value("suit").isDouble()
        || !spec.value("number").isDouble())) || !spec.value("subcards").isArray())
        return failure("conversion proposal lacks exact printed identity or payment");
    if ((spec.contains("user_string") && !spec.value("user_string").isString())
        || (spec.contains("skill") && !spec.value("skill").isString()))
        return failure("conversion proposal declaration or skill is not a string");
    const QJsonObject identity = scope.value("identity").toObject();
    const SkillInstanceRef activationRef = responseRef(identity, "activation");
    const SkillInstanceRef sourceRef = responseRef(identity, "source");
    if (activationRef.key.skillName == "ol2shanjia" || activationRef.key.skillName == "jiewan") {
        AiSkillActionContext exact;
        exact.activationRef = activationRef; exact.sourceRef = sourceRef;
        if (!skillAction || !aiRound52OlspQuestion(request, player)
            || !aiRound52OlspAction(m_room, player, exact))
            return failure("OL/SP response source inquiry changed");
    }
    if (activationRef.ownerObjectName != player->objectName()
        || !player->hasSkillInstance(activationRef.key.skillName, activationRef.key.instanceID)
        || player->isSkillInvalid(activationRef.key.skillName, activationRef.key.instanceID))
        return failure("conversion activation instance is no longer usable");
    SkillInstanceRef currentRoot = m_skillRuntime.resolveSkillInstanceRootRef(activationRef);
    if (!currentRoot.isValid()) currentRoot = activationRef;
    if (currentRoot != sourceRef) return failure("conversion borrowed source changed");
    const ViewAsSkill *skill = Sanguosha->getViewAsSkill(scope.value("skill_name").toString());
    if (!skill) return failure("conversion source has no view-as rule");
    const auto *v2 = dynamic_cast<const ViewAsSkillV2 *>(skill);
    if ((v2 ? 2 : 1) != scope.value("version").toInt())
        return failure("conversion producer version changed");
    const CardUseStruct::CardUseReason reason = CardUseStruct::CardUseReason(scope.value("reason").toInt());
    const QString pattern = scope.value("pattern").toString();
    EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
    LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
    ResponseNativeScope responseScope(m_room, player, reason, pattern);
    if (legacyResponse) {
        legacyPrototype.reset(legacyPrototypeName.startsWith('#')
            ? LuaSkillCard::Parse(legacyPrototypeName + ":.:") : Sanguosha->cloneSkillCard(legacyPrototypeName));
        if (!legacyPrototype) return failure("legacy response prototype unavailable");
        // Fixed basara hegemonyRuleCard constructs this proxy then sets its
        // raw skill. Preserve that source policy on the selected receipt;
        // Careerman's later Card::Parse path remains a separate incompatibility.
        if (legacyPrototypeName == "ActiveSkillCard" && scope.value("skill_name") == "heg_firstshow")
            legacyPrototype->setSkillName("heg_firstshow");
        // Fixed Original common44-49 constructs ActiveSkillCard then writes
        // heg_kurou before adding its exact selected material. The readonly
        // prototype path already preserves this raw skill; selected policy
        // metadata must describe that same source product. Native factory,
        // ordered material validation and authority ticket stay unchanged.
        if (legacyPrototypeName == "ActiveSkillCard"
            && aiRound36KurouPrototype(request, player, scope.value("skill_name").toString(), legacyPrototypeName) == 1)
            legacyPrototype->setSkillName("heg_kurou");
        if (legacyPrototypeName == "ActiveSkillCard"
            && aiRound43ChoicePrototype(request, player, scope.value("skill_name").toString(), legacyPrototypeName) == 2)
            legacyPrototype->setSkillName("mtjieli");
        if (spec.contains("user_string")) legacyPrototype->setUserString(spec.value("user_string").toString());
    }
    if (scope.value("play").toBool()
        && (request.kind != AIRequest::Activate || request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            || reason != request.reason || pattern != request.pattern || !skill->isEnabledAtPlay(player)))
        return failure("conversion play enablement changed");
    if (!scope.value("play").toBool() && !scope.value("has_action").toBool()
        && !skill->isEnabledAtResponse(player, scope.value("enabled_pattern").toString()))
        return failure("conversion response enablement changed");
    const bool jincaiwangGenerated = v2 && activationRef.key.skillName == "secondjincaiwang"
        && !skillAction && !legacyResponse && !selectedMaterialProposal;
    QList<int> payable = player->handCards();
    for (const Card *card : player->getEquips()) if (card) payable << card->getEffectiveId();
    for (int id : player->getHandPile()) if (!payable.contains(id)) payable << id;
    const QList<int> expanded = skill->getExpandPileCardIds(player);
    for (int id : expanded) if (!payable.contains(id)) payable << id;
    const bool feiyangJudging = activationRef.key.skillName == "feiyang" && request.kind == AIRequest::UseCard
        && request.pattern == "@@feiyang" && player->getPhase() == Player::Judge;
    if (feiyangJudging) for (int id : player->getJudgingAreaID()) if (!payable.contains(id)) payable << id;
    // A guessed ID from an unprojected pile cannot become a disclosure oracle.
    QSet<int> visible;
    for (const AICardView &card : request.worldView.handCards) visible.insert(card.effectiveId);
    for (const AICardView &card : request.worldView.self.equips) visible.insert(card.effectiveId);
    if (feiyangJudging) for (const AICardView &card : request.worldView.self.judgingArea) visible.insert(card.effectiveId);
    if (jincaiwangGenerated && spec.value("name") == "slash")
        for (const AICardView &card : request.worldView.self.judgingArea) {
            payable << card.effectiveId; visible.insert(card.effectiveId);
        }
    for (const AICardPileView &pile : request.worldView.self.piles)
        if (pile.open) for (int id : pile.cardIds) visible.insert(id);
    for (const AIPlayerView &other : request.worldView.players) {
        for (const AICardView &card : other.equips) visible.insert(card.effectiveId);
        for (const AICardView &card : other.judgingArea) visible.insert(card.effectiveId);
        for (const AICardView &card : other.knownCards) visible.insert(card.effectiveId);
        for (const AICardPileView &pile : other.piles)
            if (pile.open) for (int id : pile.cardIds) visible.insert(id);
    }
    for (const AICardView &card : request.worldView.discardPile) visible.insert(card.effectiveId);
    const auto notifiedPiles = request.worldView.strategyFacts.value("notified_piles").toObject();
    for (auto it = notifiedPiles.begin(); it != notifiedPiles.end(); ++it)
        for (const QJsonValue &id : it.value().toObject().value("current_card_ids").toArray()) visible.insert(id.toInt(-1));
    QList<int> previewCosts;
    for (const auto &value : spec.value("subcards").toArray()) {
        const double n = value.toDouble(-1);
        if (!value.isDouble() || n < 0 || n != std::floor(n) || n > std::numeric_limits<int>::max())
            return failure("invalid conversion payment ID");
        previewCosts << int(n);
    }
    const bool previewPayment = (activationRef.key.skillName == "PlusYiji" || activationRef.key.skillName == "fatewangzhe")
        && authorizeAIStrategyPreviewPayment(request, m_room, player, activationRef, sourceRef, previewCosts);
    if (previewPayment) for (int id : previewCosts) { payable << id; visible.insert(id); }
    QList<int> newgeneralsForeign;
    if(!aiRound45NotifiedSkill(request).isEmpty() && activationRef.key.skillName==aiRound45NotifiedSkill(request)
        && !authorizeAIRound45NotifiedPayment(request,m_room,player,activationRef,sourceRef,previewCosts,&newgeneralsForeign))
        return rejectedPayment("Newgenerals notified payment changed or lacks current disclosure");
    for(int id:newgeneralsForeign){payable<<id;visible.insert(id);}
    const QString leiSkill=activationRef.key.skillName;
    const bool leiExchange=request.kind==AIRequest::UseCard && request.pattern=="@@"+leiSkill
        && (leiSkill=="poxi"||leiSkill=="hongju"||leiSkill=="olhongju"||leiSkill=="mobilehongju");
    QList<int> leiForeign;
    if(leiExchange && !authorizeAIRound44LeiPayment(request,m_room,player,activationRef,sourceRef,previewCosts,&leiForeign))
        return rejectedPayment("Lei exchange source/current ordered materials changed");
    for(int id:leiForeign){payable<<id;visible.insert(id);}
    QList<int> chumingPublicMaterials;
    const bool chumingPayment = selectedMaterialProposal && selectedMaterials.isEmpty()
        && authorizeAIRound24ChumingPayment(request, player, activationRef, sourceRef, spec, &chumingPublicMaterials);
    if (chumingPayment) for (int id : chumingPublicMaterials) { payable << id; visible.insert(id); }
    QList<int> costs;
    QSet<int> unique;
    for (const QJsonValue &value : spec.value("subcards").toArray()) {
        const double number = value.toDouble(-1);
        if (!value.isDouble() || number < 0 || number != std::floor(number)
            || number > std::numeric_limits<int>::max()) return failure("invalid conversion payment ID");
        const int id = int(number);
        if (unique.contains(id) || !payable.contains(id) || !visible.contains(id))
            return failure("conversion payment is duplicate, hidden, or outside its source");
        unique.insert(id); costs << id;
    }
    const bool niwoSourcePayment = skillAction && legacyPrototypeName == "#ov_niwoCard"
        && activationRef.key.skillName == "ov_niwo" && sourceRef.key.skillName == "ov_niwo"
        && aiRound28NiwoResponseQuestion(request,m_room,player);
    if (costs.size() > (niwoSourcePayment ? 998 : 128)) return failure("conversion payment budget exhausted");
    if (jincaiwangGenerated && !round25JincaiwangMaterial(request, m_room, player,
            spec.value("name").toString(), costs))
        return failure("SecondJincaiwang sole disclosed material changed");
    if (legacyPrototype) legacyPrototype->addSubcards(costs);
    const Card *produced = nullptr;
    const bool manjuanRaw = legacyResponse && legacyPrototypeName == "#kezhuanmanjuanVsCard"
        && activationRef.key.skillName == "kezhuanmanjuan";
    int manjuanRawId = -1;
    QString declaration;
    const bool zhangwuGenerated = v2 && aiRound27ZhangwuGeneratedBranch(request, player, activationRef.key.skillName);
    const bool zhangwuPrinted = v2 && request.kind == AIRequest::Activate
        && reason == CardUseStruct::CARD_USE_REASON_PLAY && activationRef.key.skillName == "lxtx_zhangwu";
    if (zhangwuGenerated && !aiRound27ZhangwuGeneratedPayment(request, player, activationRef.key.skillName, costs))
        return rejectedPayment("Zhangwu source proposal differs from native generated ordered payment");
    if (manjuanRaw) {
        if (!costs.isEmpty()) return failure("Manjuan fixed source wire has no selected costs");
        declaration = spec.value("user_string").toString();
        produced = constructAIRound53ManjuanRaw(request,player,declaration,&manjuanRawId);
        if (!produced) return failure("Manjuan raw response lacks its current actual source selection");
    } else if (v2) {
        AiSkillActionContext current;
        SkillInstance instance;
        bool found = false;
        for (const SkillInstance &candidate : player->getSkillInstances())
            if (candidate.key() == activationRef.key) { instance = candidate; found = true; break; }
        if (!found || !buildSkillActionContext(player, instance, reason, pattern, current)
            || current.sourceRef != sourceRef) return failure("conversion source quota or activation changed");
        ActiveSkillRequest activation;
        activation.reason = reason; activation.pattern = pattern; activation.initiator = player;
        activation.activationRef = activationRef; activation.sourceRef = sourceRef;
        declaration = spec.value("user_string").toString();
        if (declaration.isEmpty() && v2->declaresCardName()) declaration = spec.value("name").toString();
        activation.userString = declaration;
        NotifiedPileQueryScope notifiedPile(activation, v2);
        const bool generatedHand = activationRef.key.skillName == "chopper" || activationRef.key.skillName == "tiaojiaoCMT";
        if (generatedHand && (costs != player->handCards()
            || (activationRef.key.skillName == "chopper" && (costs.isEmpty() || spec.value("name").toString() != "slash"))
            || (activationRef.key.skillName == "tiaojiaoCMT" && (!skillAction || request.kind != AIRequest::Activate))))
            return failure("generated-hand skill requires its unchanged full payment and action scope");
        for (int id : (generatedHand || zhangwuGenerated || jincaiwangGenerated) ? QList<int>() : selectedMaterialProposal ? selectedMaterials : costs) {
            const Card *card = Sanguosha->getCard(id);
            if (!card) return failure("conversion payment card is unavailable");
            if (!v2->canSelectCard(activation, card)) return rejectedPayment("conversion payment prefix rejected");
            activation.selectedCardIds << id;
        }
        if (!v2->cardSelectionFeasible(activation)) return rejectedPayment("conversion payment is infeasible");
        produced = v2->createCard(activation);
    } else {
        if (skillAction) {
            AiSkillActionContext current;
            bool valid = false;
            for (const auto &instance : player->getSkillInstances())
                if (instance.key() == activationRef.key)
                    valid = buildSkillActionContext(player, instance, reason, pattern, current)
                        && current.sourceRef == sourceRef;
            if (!valid) return failure("V1 skill action activation changed");
        }
        ResponseDeclarationScope declarationScope;
        if (!declarationScope.prepare(skill->objectName(), player, reason, pattern, activationRef,
            spec.contains("user_string") ? spec.value("user_string").toString() : spec.value("name").toString(),
            spec.contains("user_string"))) return failure("V1 declaration rejected by native metadata");
        declaration = declarationScope.declaration();
        QList<const Card *> selected;
        for (int id : costs) {
            const Card *card = Sanguosha->getCard(id);
            if (!card) return failure("V1 conversion payment card is unavailable");
            if (!skill->viewFilter(selected, card)) return rejectedPayment("V1 conversion payment prefix rejected");
            selected << card;
        }
        if (legacyResponse && legacyPrototypeName == "#tongyiTQ"
            && isAIRound33TongyiSource(request, activationRef.key.skillName, className)) {
            produced = buildAIRound33TongyiSource(request, m_room, player, activationRef, sourceRef,
                className, spec.value("user_string").toString(), costs);
        } else produced = skill->viewAs(selected);
        if (!declarationScope.retainProduced(produced)) return failure("V1 declaration clone ownership changed");
        if (const auto *proxy = dynamic_cast<const SkillCard *>(produced)) declaration = proxy->getUserString();
        if (spec.contains("user_string") && declaration != spec.value("user_string").toString()) {
            if (produced && produced->isVirtualCard()) const_cast<Card *>(produced)->deleteLater();
            return failure("V1 declaration was not produced by its authority rule");
        }
    }
    if (!produced) return failure("conversion rule did not produce the proposed card");
    const bool skinGenerated = v2 && skillAction && legacyResponse
        && legacyPrototypeName == "#skin" && activationRef.key.skillName == "skin";
    if (skinGenerated && (!declaration.isEmpty() || !costs.isEmpty()
        || !aiRound44SkinProduct(request, player, activationRef, sourceRef, produced, legacyPrototype.get()))) {
        if (produced->isVirtualCard()) const_cast<Card *>(produced)->deleteLater();
        return failure("Skin generated product does not match exact empty source policy");
    }
    const auto discardProduced = [&]() {
        if (produced->isVirtualCard()) const_cast<Card *>(produced)->deleteLater();
    };
    if (!skillAction && !legacyResponse && (produced->objectName() != spec.value("name").toString()
        || int(produced->getSuit()) != spec.value("suit").toDouble()
        || produced->getNumber() != spec.value("number").toDouble())) {
        discardProduced(); return failure("conversion rule produced a different printed identity");
    }
    const bool round33Physical = v2 && aiRound33PlanningPhysicalScope(request,activationRef.key.skillName)
        && produced->isKindOf("EquipCard");
    if (round33Physical && !authorizeAIRound33PlanningPhysicalProduct(request,m_room,player,
            activationRef,sourceRef,costs,produced)) {
        discardProduced(); return failure("sfofl owner-pile physical Equip authentication failed");
    }
    const bool physicalProduct = round33Physical || (skillAction && v2
        && isAuditedPhysicalCardSkill(activationRef.key.skillName, reason, pattern));
    if (round33Physical) {
        // Exact original owner-pile product was authenticated above; no physical mutation.
    } else if (physicalProduct && isMarkedPhysicalCardSkill(activationRef.key.skillName, reason, pattern)) {
        bool printed = false; QJsonObject disclosed;
        const int id = produced->getEffectiveId();
        if (produced->isVirtualCard() || !costs.isEmpty()
            || !authorizeAIOlClanMarkedPhysicalProduct(request, id, &disclosed, &printed)
            || produced != (printed ? Sanguosha->getEngineCard(id) : Sanguosha->getCard(id))
            || (!printed && (produced->objectName() != disclosed.value("name").toString()
                || produced->getSuit() != disclosed.value("suit").toInt(-1)
                || produced->getNumber() != disclosed.value("number").toInt(-1)))) {
            discardProduced(); return failure("marked physical producer lacks unchanged disclosed authority");
        }
    } else if (physicalProduct) {
        if (produced->isVirtualCard() || costs.size() != 1 || produced != Sanguosha->getCard(costs.first())
            || produced->getEffectiveId() != costs.first()
            || !authorizeAIOlClanNotifiedEvaluation(request, costs.first())) {
            discardProduced(); return failure("notified producer did not preserve the original disclosed physical card");
        }
    } else if (jincaiwangGenerated) {
        if (!round25JincaiwangProduct(produced, spec.value("name").toString(), costs)) {
            discardProduced(); return failure("SecondJincaiwang native material tag or zero-cost product changed");
        }
    } else if (produced->getSubcards() != costs) {
        discardProduced();
        if (zhangwuPrinted) return rejectedPayment("Zhangwu native product changed the proposed payment sequence");
        return failure("conversion rule did not preserve the proposed payment sequence");
    }
    if (zhangwuPrinted && (spec.value("skill").toString() != "lxtx_zhangwuu"
        || produced->getSkillName(false) != "lxtx_zhangwuu")) {
        discardProduced(); return failure("Zhangwu printed skill attribution changed");
    }
    if (!legacyResponse && !scope.value("has_action").toBool() && spec.contains("skill")
        && spec.value("skill").toString() != produced->getSkillName(false)) {
        discardProduced(); return failure("conversion rule produced a different skill attribution");
    }
    if (skillAction && !physicalProduct) {
        Card *bound = const_cast<Card *>(produced);
        bound->setActivationSkill(activationRef.key.skillName, activationRef.key.instanceID);
        bound->setSourceSkill(sourceRef.key.skillName, sourceRef.key.instanceID);
    }
    QJsonObject chopperFlags;
    const bool chopperFlagsKnown = v2 && activationRef.key.skillName == "chopper"
        && !physicalProduct && produced->isVirtualCard() && produced->objectName() == "slash"
        && !costs.isEmpty() && costs == player->handCards();
    const bool fuluFlagsKnown = v2 && activationRef.key.skillName == "fulu"
        && !physicalProduct && costs.size() == 1 && produced->isVirtualCard()
        && produced->objectName() == "thunder_slash" && produced->getSkillName() == "fulu"
        && produced->getSubcards() == costs && m_room.getCardOwner(costs.first()) == player;
    if (chopperFlagsKnown || fuluFlagsKnown)
        for (const QString &flag : produced->getFlags()) chopperFlags.insert(flag, true);
    int budget = 4096;
    // Chuming's exact accepted V2 scope names an activation, not a printed
    // card expression. Its live source/state/public-material proof was checked
    // above. Keep response semantics and normal limited/target predicates;
    // do not substitute Play availability for this response.
    // The audited Tongyi source returns a SkillCard whose response expression
    // is tested on the donor's class dummy below. Enumerate targets on the REAL
    // already-proven SkillCard, not on that dummy and not on a mismatching raw
    // SkillCard expression. All live limits/targetFilter/feasibility still apply.
    const bool round33TongyiTargets = request.kind == AIRequest::UseCard && !skillAction
        && legacyResponse && legacyPrototypeName == "#tongyiTQ"
        && isAIRound33TongyiSource(request, activationRef.key.skillName, className);
    AIRound47XufaSourceCard xufaSource(m_room,parentRequest,player,produced,activationRef.key.skillName);
    const AICardCandidateView candidate = makeAICardCandidate(m_room, player, produced,
        request.handlingMethod, skillAction ? AIRequest::Activate : request.kind,
        (chumingPayment || round33TongyiTargets) ? QString() : request.pattern, budget);
    AICardConversionView ticket;
    ticket.conversionId = request.cardConversions.size() + request.conversionLedger->tickets.size() + 1;
    ticket.hasSelectedMaterials = selectedMaterialProposal || jincaiwangGenerated;
    ticket.selectedMaterialIds = jincaiwangGenerated ? costs : selectedMaterials;
    ticket.manjuanRawSource = manjuanRaw; ticket.manjuanRawSelectedId = manjuanRawId;
    ticket.producerVersion = v2 ? 2 : 1; ticket.producerReason = reason;
    ticket.producerPattern = pattern; ticket.declaredName = declaration; ticket.policyClass = className;
    ticket.skinGeneratedDeclaration = skinGenerated;
    if (skinGenerated) ticket.skinGeneratedUserString = static_cast<const SkillCard *>(produced)->getUserString();
    ticket.legacyIdentityToken = aiLegacyCardIdentity(m_room, player, produced);
    ticket.nameLength = produced->nameLength();
    ticket.legacyStringKey = produced->toString();
    ticket.legacySortKey = legacySortKey(produced);
    ticket.name = produced->objectName(); ticket.className = produced->getClassName();
    ticket.kindOfNames = produced->getKindOfNames(); ticket.suit = int(produced->getSuit());
    ticket.number = produced->getNumber(); ticket.typeId = produced->getTypeId();
    ticket.handlingMethod = int(produced->getHandlingMethod());
    ticket.targetResidue = responseResidue(m_room, player, produced);
    ticket.cardRules = responseOwnerRules(m_room, player, produced);
    if (physicalProduct) ticket.cardRules.insert("physical_product_id", produced->getEffectiveId());
    if (chopperFlagsKnown || fuluFlagsKnown) {
        ticket.cardRules.insert("known_flags", chopperFlags);
        ticket.cardRules.insert("flags_complete", true);
    }
    const bool ownSkillChain = !physicalProduct || m_room.getCardOwner(produced->getEffectiveId()) == player;
    ticket.cardRules.insert("skill_names_complete", ownSkillChain);
    if (ownSkillChain) ticket.cardRules.insert("skill_names", QJsonArray::fromStringList(produced->getSkillNames()));
    if (skinGenerated) {
        ticket.legacyStringKey = legacyPrototype->toString();
        ticket.legacySortKey = legacySortKey(legacyPrototype.get());
        ticket.cardRules.insert("legacy_key_parts", legacyKeyParts(legacyPrototype.get()));
    }
    // OL2Shanjia's exact V1 response factory deliberately returns the raw
    // _ol2shanjia name. Its activation identity remains the authenticated
    // ol2shanjia instance; do not rewrite the donor/native product face.
    const bool ol2ResponseRaw = skillAction && !v2 && activationRef.key.skillName == "ol2shanjia"
        && aiRound52OlspQuestion(request, player);
    const bool tyfenwuRaw = v2 && aiRound53TyfenwuRawProduct(request,player,activationRef.key.skillName,produced);
    ticket.skillName = scope.value("has_action").toBool() && !physicalProduct && !zhangwuPrinted && !ol2ResponseRaw && !manjuanRaw && !tyfenwuRaw
        ? activationRef.key.skillName : produced->getSkillName(false);
    ticket.canRecast = produced->canRecast(); ticket.willThrow = produced->willThrow();
    ticket.usingCard = produced->hasFlag("using"); ticket.locked = player->isLocked(produced);
    ticket.containsJinchantuoqiao = (skinGenerated ? legacyPrototype.get() : produced)->toString().contains("jinchantuoqiao");
    ticket.damageCard = produced->isDamageCard(); ticket.singleTarget = produced->isSingleTargetCard(); ticket.red = produced->isRed(); ticket.black = produced->isBlack();
    ticket.activationRef = activationRef; ticket.sourceRef = sourceRef;
    std::unique_ptr<Card> olClanPolicyTemplate;
    if (scope.value("has_action").toBool() && !skillAction && !legacyResponse) {
        std::unique_ptr<Card> viewed(Sanguosha->cloneCard(produced));
        if (!viewed) { discardProduced(); return failure("conversion display identity unavailable"); }
        // Engine's general clone copies effectiveId and omits subcards. That
        // would turn a one-material virtual conversion into a physical card
        // and replace its policy string with the material ID. This display
        // copy must retain the actual product shape and ordered payment.
        viewed->setId(produced->getId());
        viewed->clearSubcards();
        viewed->addSubcards(produced->getSubcards());
        viewed->setSkillName(zhangwuPrinted ? produced->getSkillName(false) : activationRef.key.skillName);
        viewed->setActivationSkill(activationRef.key.skillName, activationRef.key.instanceID);
        viewed->setSourceSkill(sourceRef.key.skillName, sourceRef.key.instanceID);
        ticket.legacyStringKey = viewed->toString();
        ticket.legacySortKey = legacySortKey(viewed.get());
        ticket.cardRules.insert("legacy_key_parts", legacyKeyParts(viewed.get()));
        if(needsOlClanObservationCustody(request,player) && (ticket.className=="GodSalvation" || ticket.policyClass=="GodSalvation"))
            olClanPolicyTemplate=std::move(viewed);
    }
    ticket.activationQuotaAvailable = ticket.sourceQuotaAvailable = true;
    ticket.generatedHandPayment = v2 && (activationRef.key.skillName == "chopper" || activationRef.key.skillName == "tiaojiaoCMT" || zhangwuGenerated);
    ticket.subcardIds = jincaiwangGenerated ? QList<int>() : costs; ticket.available = candidate.available && !candidate.limited;
    ticket.matchesResponsePattern = candidate.available;
    ticket.sortingOnly = !ticket.matchesResponsePattern;
    ticket.extraTargetKnown = candidate.extraTargetKnown; ticket.extraTargetCount = candidate.extraTargetCount;
    ticket.extraTargetByPlayer = candidate.extraTargetByPlayer;
    ticket.targetFilterEmpty = candidate.targetFilterEmpty; ticket.targetFixed = candidate.targetFixed;
    ticket.feasibleWithNoTarget = candidate.feasibleWithNoTarget; ticket.completeCoverage = candidate.completeCoverage;
    ticket.legalTargets = candidate.legalTargets; ticket.maxVotes = candidate.maxVotes;
    ticket.targetCombinations = candidate.targetCombinations;
    ticket.affectedTargetsKnown = candidate.affectedTargetsKnown; ticket.affectedTargets = candidate.affectedTargets;
    const Card *classPolicyCard = legacyPrototype ? legacyPrototype.get() : produced;
    if (classPolicyCard->getTypeId() < 1 && request.kind == AIRequest::UseCard) {
        ticket.responseClassPlanning = QJsonObject{{"exists", false}};
        // The donor loop receives its legacy policy object, which can differ
        // from the authority-produced card represented by this receipt.
        const QString name = classPolicyCard->toString().section(':', -1);
        std::unique_ptr<Card> proxy(Sanguosha->cloneCard(name, Card::NoSuit, 0));
        if (proxy) {
            if (!classPolicyCard->willThrow()) proxy->addSubcards(classPolicyCard->getSubcards());
            const auto candidate = makeAICardCandidate(m_room, player, proxy.get(),
                request.handlingMethod, request.kind, request.pattern, budget);
            QJsonObject planning = publicDecisionCard(proxy.get(), true);
            planning.insert("evaluation_only", true); planning.insert("virtual_card", true);
            planning.insert("can_recast", proxy->canRecast()); planning.insert("locked", player->isLocked(proxy.get()));
            planning.insert("using", proxy->hasFlag("using")); planning.insert("subcards", responseIds(proxy->getSubcards()));
            planning.insert("target_fixed", proxy->targetFixed()); planning.insert("handling_method", int(proxy->getHandlingMethod()));
            planning.insert("target_residue", responseResidueJson(responseResidue(m_room, player, proxy.get())));
            planning.insert("evaluation_rules", responseOwnerRules(m_room, player, proxy.get()));
            planning.insert("evaluation_owner", player->objectName());
            const auto targetData = responseCandidateJson(candidate);
            for (auto it = targetData.begin(); it != targetData.end(); ++it) planning.insert(it.key(), it.value());
            ticket.responseClassPlanning = QJsonObject{{"exists", true}, {"card", planning}, {"candidate", targetData}};
        }
    }
    if (produced->getTypeId() < 1 && (request.kind == AIRequest::UseCard || request.kind == AIRequest::RespondCard)) {
        Card *proxy = Sanguosha->cloneCard(scope.value("card_name").toString());
        if (proxy) {
            proxy->setSkillName(ticket.skillName);
            if (!produced->willThrow()) proxy->addSubcards(produced->getSubcards());
            const AICardCandidateView proxyCandidate = makeAICardCandidate(m_room, player, proxy,
                request.handlingMethod, request.kind, request.pattern, budget);
            QJsonObject planning = publicDecisionCard(proxy, true);
            planning.insert("evaluation_only", true); planning.insert("virtual_card", true);
            planning.insert("can_recast", proxy->canRecast()); planning.insert("locked", player->isLocked(proxy));
            planning.insert("using", proxy->hasFlag("using")); planning.insert("subcards", responseIds(proxy->getSubcards()));
            planning.insert("target_fixed", proxy->targetFixed()); planning.insert("handling_method", int(proxy->getHandlingMethod()));
            planning.insert("target_residue", responseResidueJson(responseResidue(m_room, player, proxy)));
            planning.insert("evaluation_rules", responseOwnerRules(m_room, player, proxy));
            planning.insert("evaluation_owner", player->objectName());
            const QJsonObject targetData = responseCandidateJson(proxyCandidate);
            for (auto it = targetData.begin(); it != targetData.end(); ++it) planning.insert(it.key(), it.value());
            planning.insert("matches_response_pattern", proxyCandidate.available);
            ticket.responsePlanning = QJsonObject{{"card", planning}, {"candidate", targetData}};
            ticket.matchesResponsePattern = proxyCandidate.available;
            ticket.available = proxyCandidate.available && !proxyCandidate.limited && !player->isLocked(proxy);
            ticket.sortingOnly = !ticket.matchesResponsePattern;
            proxy->deleteLater();
        }
    }
    if (produced->getTypeId() < 1) {
        // standard_cards' Slash wrapper is a separate donor dummy: no skill
        // attribution, all costs, and the SkillCard's effective suit/number.
        // It never supplies authorization or replaces responsePlanning.
        const QString printed = produced->toString().section("->", 0, 0);
        const QString wrappedName = printed.section(':', -1);
        std::unique_ptr<Card> wrapped(Sanguosha->cloneCard(wrappedName, Card::NoSuit, 0));
        if (wrapped) {
            wrapped->addSubcards(produced->getSubcards());
            wrapped->setNumber(produced->getNumber()); wrapped->setSuit(produced->getSuit());
            const AICardCandidateView wrappedCandidate = makeAICardCandidate(m_room, player, wrapped.get(),
                request.handlingMethod, request.kind, request.pattern, budget);
            QJsonObject row = publicDecisionCard(wrapped.get(), true);
            row.insert("evaluation_only", true); row.insert("virtual_card", true);
            row.insert("effective_id", wrapped->getEffectiveId()); row.insert("subcards", responseIds(wrapped->getSubcards()));
            row.insert("can_recast", wrapped->canRecast()); row.insert("locked", player->isLocked(wrapped.get()));
            row.insert("using", wrapped->hasFlag("using")); row.insert("target_fixed", wrapped->targetFixed());
            row.insert("handling_method", int(wrapped->getHandlingMethod()));
            row.insert("target_residue", responseResidueJson(responseResidue(m_room, player, wrapped.get())));
            row.insert("evaluation_rules", responseOwnerRules(m_room, player, wrapped.get()));
            row.insert("evaluation_owner", player->objectName());
            const auto targetData = responseCandidateJson(wrappedCandidate);
            for (auto it = targetData.begin(); it != targetData.end(); ++it) row.insert(it.key(), it.value());
            ticket.cardRules.insert("slash_wrapper_planning", QJsonObject{{"card", row}, {"candidate", targetData}});
        }
    }
    // A policy-only clone reproduces the donor's addSubcard material identity.
    // The authoritative ticket remains the unmodified native zero-cost product.
    QJsonObject sourceMaterialPolicy;
    if (jincaiwangGenerated) {
        std::unique_ptr<Card> policy(Sanguosha->cloneCard(produced->objectName(), produced->getSuit(), produced->getNumber()));
        if (!policy) { discardProduced(); return failure("SecondJincaiwang source policy clone unavailable"); }
        policy->setSkillName("secondjincaiwang"); policy->addSubcards(costs);
        sourceMaterialPolicy = publicDecisionCard(policy.get(), true);
        sourceMaterialPolicy.insert("subcards", responseIds(costs));
        sourceMaterialPolicy.insert("virtual_card", true);
        sourceMaterialPolicy.insert("evaluation_only", true);
        sourceMaterialPolicy.insert("effective_id", policy->getEffectiveId());
        sourceMaterialPolicy.insert("legacy_string_key", policy->toString());
        sourceMaterialPolicy.insert("legacy_sort_key", legacySortKey(policy.get()));
        sourceMaterialPolicy.insert("can_recast", policy->canRecast());
        sourceMaterialPolicy.insert("handling_method", int(policy->getHandlingMethod()));
        sourceMaterialPolicy.insert("locked", player->isLocked(policy.get()));
        sourceMaterialPolicy.insert("using", policy->hasFlag("using"));
        sourceMaterialPolicy.insert("target_fixed", policy->targetFixed());
        sourceMaterialPolicy.insert("evaluation_owner", player->objectName());
        sourceMaterialPolicy.insert("evaluation_rules", responseOwnerRules(m_room, player, policy.get()));
        sourceMaterialPolicy.insert("target_residue", responseResidueJson(responseResidue(m_room, player, policy.get())));
    }
    QJsonObject legacyPolicy;
    if (legacyPrototype) {
        legacyPolicy = publicDecisionCard(legacyPrototype.get(), true);
        legacyPolicy.insert("legacy_selector", legacyPrototypeName);
        // Preserve object identity as an opaque request-local negative value.
        // The process-wide Card allocator counter is not viewer information.
        legacyPolicy.insert("id", -ticket.conversionId);
        legacyPolicy.insert("effective_id", costs.isEmpty() ? -ticket.conversionId : costs.first());
        legacyPolicy.insert("jilei", player->isJilei(legacyPrototype.get()));
        legacyPolicy.insert("subcards", responseIds(legacyPrototype->getSubcards()));
        legacyPolicy.insert("can_recast", legacyPrototype->canRecast());
        legacyPolicy.insert("handling_method", int(legacyPrototype->getHandlingMethod()));
        legacyPolicy.insert("locked", player->isLocked(legacyPrototype.get()));
        legacyPolicy.insert("using", legacyPrototype->hasFlag("using"));
        legacyPolicy.insert("target_fixed", legacyPrototype->targetFixed());
        legacyPolicy.insert("legacy_string_key", legacyPrototype->toString());
        legacyPolicy.insert("legacy_sort_key", legacySortKey(legacyPrototype.get()));
        legacyPolicy.insert("evaluation_owner", player->objectName());
        QJsonObject limits;
        for (int method = int(Card::MethodNone); method <= int(Card::MethodGet); ++method)
            limits.insert(QString::number(method), player->isCardLimited(legacyPrototype.get(), Card::HandlingMethod(method)));
        legacyPolicy.insert("evaluation_rules", QJsonObject{{"limits", limits},
            {"available", legacyPrototype->isAvailable(player)},
            {"legacy_key_parts", legacyKeyParts(legacyPrototype.get())},
            {"target_mod_residue", Sanguosha->correctCardTarget(TargetModSkill::Residue, player, legacyPrototype.get())}});
        if (legacyPrototype->isKindOf("Analeptic")) {
            auto rules = legacyPolicy.value("evaluation_rules").toObject();
            rules.insert("analeptic_static_available", Analeptic::IsAvailable(player, legacyPrototype.get()));
            legacyPolicy.insert("evaluation_rules", rules);
        }
    }
    if (!aiRound44RangeRequestCurrent(m_room,parentRequest)) {
        discardProduced();return failure("conversion authority rule changed game state");
    }
    if(needsOlClanObservationCustody(request,player) && (ticket.className=="GodSalvation" || ticket.policyClass=="GodSalvation")
        && ticket.conversionId<=std::numeric_limits<int>::max()/2) {
        if(olClanPolicyTemplate){
            auto policyLease=AIOwnerReadonlyCardStore::own(std::move(olClanPolicyTemplate));
            if(policyLease)request.conversionLedger->observedProducts.insert(-2*ticket.conversionId,std::move(policyLease));
        }
        if(legacyPrototype){
            auto policyLease=AIOwnerReadonlyCardStore::own(std::move(legacyPrototype));
            if(policyLease)request.conversionLedger->observedProducts.insert(-2*ticket.conversionId+1,std::move(policyLease));
        }
        m_room.roomRuntime()->ai().trackObservationLedger(request.conversionLedger);
    }
    if(manjuanRaw) {
        // This exact raw precursor was freshly allocated by Card::Parse, not
        // borrowed from a physical card owner. The finite owning lease pins
        // its canonical deferred-deletion graph. own() consumes even on failure.
        auto lease=AIOwnerReadonlyCardStore::own(std::unique_ptr<Card>(const_cast<Card *>(produced)));
        if(!lease)return failure("Manjuan raw source custody unavailable");
        request.conversionLedger->observedProducts.insert(ticket.conversionId,std::move(lease));
        m_room.roomRuntime()->ai().trackObservationLedger(request.conversionLedger);
    } else if(needsOlClanObservationCustody(request,player)) {
        AIReadonlyCardLease lease=produced->isVirtualCard()
            ? AIOwnerReadonlyCardStore::own(std::unique_ptr<Card>(const_cast<Card *>(produced)))
            : AIOwnerReadonlyCardStore::observe(produced);
        // own consumes the unique_ptr on success AND failure; no second delete.
        if(lease){
            request.conversionLedger->observedProducts.insert(ticket.conversionId,std::move(lease));
            m_room.roomRuntime()->ai().trackObservationLedger(request.conversionLedger);
        }
    } else discardProduced();
    request.conversionLedger->tickets << ticket;
    QJsonObject result{{"ticket", responseTicketJson(ticket)}};
    if (!legacyPolicy.isEmpty()) result.insert("legacy_policy", legacyPolicy);
    if (!sourceMaterialPolicy.isEmpty()) result.insert("source_material_policy", sourceMaterialPolicy);
    return result;
}

bool AiDecisionCoordinator::buildSkillActionContext(
    ServerPlayer *player, const SkillInstance &instance,
    CardUseStruct::CardUseReason reason, const QString &pattern,
    AiSkillActionContext &actionContext) const
{
    if (!player || !player->hasSkillInstance(instance.skillName, instance.instanceID)) return false;
    const auto nativeSkillInvalid = [&](const QString &name, int instanceId) {
        std::unique_ptr<AIRound56SkillValidityScope> validitySource;
        auto *gameLua=m_room.roomRuntime()->lua().rawState();
        if(reason==CardUseStruct::CARD_USE_REASON_PLAY&&pattern.isEmpty()
            &&!inAIRound56ProjectionScope(m_room,gameLua))
            validitySource.reset(new AIRound56SkillValidityScope(m_room,player,gameLua));
        return player->isSkillInvalid(name,instanceId);
    };
    const ViewAsSkill *native = Sanguosha->getViewAsSkill(instance.skillName);
    const ViewAsSkillV2 *skill = dynamic_cast<const ViewAsSkillV2 *>(native);
    if (!skill) {
        static const QSet<QString> auditedV1{"xiansi", "xiansi_slash", "qiaoshui", "zhiyuan", "flood", "dujiang", "smalltuxi", "PlusYiji", "fatewangzhe", "caoduo", "chuiti", "boxuan", "tanluan"};
        static const QMap<QString, QString> previewPatterns{{"PlusYiji", "@PlusYiji"}, {"fatewangzhe", "@fatewangzhe"}, {"caoduo", "@@caoduo"}, {"chuiti", "@@chuiti"}, {"boxuan", "@@boxuan"}, {"tanluan", "@@tanluan"}};
        if (previewPatterns.contains(instance.skillName)
            && (pattern != previewPatterns.value(instance.skillName)
                || (reason != CardUseStruct::CARD_USE_REASON_RESPONSE && reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE))) return false;
        const bool nestedJieyingh = instance.skillName == "jieyingh"
            && reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && pattern == "@@jieyingh";
        const bool promptedZhifeng = instance.skillName == "zhifeng"
            && reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && pattern == "@@zhifeng";
        // Fixed JijiaoVS is zero-cost V1 Play only. Retain its actual native
        // enabled/invalid-skill/root checks below; no response alias admission.
        const bool jijiaoPlay = instance.skillName == "jijiao"
            && reason == CardUseStruct::CARD_USE_REASON_PLAY && pattern.isEmpty();
        // Fixed Zunwei is a native ZeroCardViewAsSkill. Admit its genuine
        // owner instance only in Play; response scopes remain unavailable.
        // Invalidity, enabled-at-play and source-root checks still run below.
        const bool zunweiPlay = instance.skillName == "zunwei"
            && reason == CardUseStruct::CARD_USE_REASON_PLAY && pattern.isEmpty();
        const bool olspResponse = (instance.skillName == "ol2shanjia" || instance.skillName == "jiewan")
            && reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && pattern == "@@" + instance.skillName
            && aiRound52OlspInstance(m_room, player, instance.skillName, instance.instanceID);
        if (!native || (!auditedV1.contains(instance.skillName) && !nestedJieyingh && !promptedZhifeng && !jijiaoPlay && !zunweiPlay && !olspResponse)
            || nativeSkillInvalid(instance.skillName, instance.instanceID)) return false;
        const bool play = reason == CardUseStruct::CARD_USE_REASON_PLAY;
        if (play ? !native->isEnabledAtPlay(player) : !native->isEnabledAtResponse(player, pattern)) return false;
        actionContext.activationRef = SkillInstanceRef(player->objectName(), instance.key());
        actionContext.sourceRef = m_skillRuntime.resolveSkillInstanceRootRef(actionContext.activationRef);
        if (!actionContext.sourceRef.isValid()) actionContext.sourceRef = actionContext.activationRef;
        if (olspResponse && !aiRound52OlspAction(m_room, player, actionContext)) return false;
        actionContext.activationQuotaAvailable = actionContext.sourceQuotaAvailable = true;
        return true;
    }

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
    // These fixed V1 callbacks originally run in the generic response query.
    // Offer their exact payment scopes there without adding a request-present
    // dispatch (which changes the donor's nil/default path and calls it twice).
    if (instance.skillName == "ol2shanjia" || instance.skillName == "jiewan") return false;
    AiSkillActionContext actionContext;
    if (!buildSkillActionContext(player, instance, reason, pattern, actionContext))
        return false;
    aiRequest = project
        ? makeRequest(player, AIRequest::UseCard, reason, pattern, prompt, method)
        : makeRequestHeader(player, AIRequest::UseCard, reason, pattern, prompt, method);
    aiRequest.hasSkillActionContext = true;
    aiRequest.skillActionContext = actionContext;
    if (project) {
        projectAIUseCardDispatch(aiRequest, m_room, player);
        // Poxi disclosure requires this actual accepted action identity.
        // makeRequest alone has not attached that identity yet.
        projectAIRound44LeiExchange(aiRequest, m_room, player);
    }
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
    const AICardConversionView *authorized = responseTicket(request, spec.conversionId);
    if (!authorized || !authorized->available) return nullptr;
    if (authorized->manjuanRawSource) {
        EngineRuntimeContextScope engine(*Sanguosha,&m_room);LuaRuntime::Binding game(m_room.roomRuntime()->lua(),false);
        ResponseNativeScope native(m_room,player,request.reason,request.pattern);
        CardUseStruct selected;
        if(!applyAIRound53ManjuanRaw(request,player,spec,{},
            [&](CardUseStruct &use){return m_skillRuntime.resolveCardSkillInstance(use);},selected))return nullptr;
        return const_cast<Card *>(selected.card);
    }

    if (authorized->activationRef.key.skillName == "ol2shanjia"
        || authorized->activationRef.key.skillName == "jiewan") {
        AiSkillActionContext exact;
        exact.activationRef = authorized->activationRef; exact.sourceRef = authorized->sourceRef;
        if (!aiRound52OlspQuestion(request, player)
            || !aiRound52OlspAction(m_room, player, exact)) return nullptr;
    }
    if (authorized->zhifengQuestion) {
        if(spec.name!=authorized->name || spec.suit!=authorized->suit || spec.number!=authorized->number
            || !spec.skillName.isEmpty() || spec.subcardIds!=authorized->subcardIds)return nullptr;
        EngineRuntimeContextScope engine(*Sanguosha,&m_room);LuaRuntime::Binding game(m_room.roomRuntime()->lua(),false);
        ResponseNativeScope native(m_room,player,request.reason,request.pattern);
        return rebuildAIRound29ZhifengProduct(request,m_room,player,*authorized);
    }
    if (authorized->rangyiQuestion) {
        if(spec.name!=authorized->name||spec.suit!=authorized->suit||spec.number!=authorized->number
            ||!spec.skillName.isEmpty()||spec.subcardIds!=authorized->subcardIds)return nullptr;
        EngineRuntimeContextScope engine(*Sanguosha,&m_room);LuaRuntime::Binding game(m_room.roomRuntime()->lua(),false);
        return rebuildAIRound52RangyiProduct(request,m_room,player,*authorized);
    }
    if (authorized->jieyinghQuestion) {
        if (spec.name != authorized->name || spec.suit != authorized->suit
            || spec.number != authorized->number || !spec.skillName.isEmpty()
            || !spec.subcardIds.isEmpty()) return nullptr;
        EngineRuntimeContextScope engine(*Sanguosha, &m_room);
        LuaRuntime::Binding game(m_room.roomRuntime()->lua(), false);
        return rebuildAIRound27JieyinghProduct(request, m_room, player, *authorized);
    }
    if (!authorized->activationQuotaAvailable || !authorized->sourceQuotaAvailable) return nullptr;

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

    // Rebuild from the issued producer context, then independently validate the
    // actual response context. Sorting-only tickets never authorize submission.
    if (authorized->sortingOnly || (!authorized->matchesResponsePattern && request.kind != AIRequest::Activate)) return nullptr;
    EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
    LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
    const ViewAsSkill *viewAs = Sanguosha->getViewAsSkill(authorized->activationRef.key.skillName);
    if (!viewAs || authorized->activationRef.ownerObjectName != player->objectName()
        || !player->hasSkillInstance(authorized->activationRef.key.skillName,
                                     authorized->activationRef.key.instanceID)
        || player->isSkillInvalid(authorized->activationRef.key.skillName,
                                  authorized->activationRef.key.instanceID)) return nullptr;
    SkillInstanceRef source = m_skillRuntime.resolveSkillInstanceRootRef(authorized->activationRef);
    if (!source.isValid() && authorized->producerVersion == 1) source = authorized->activationRef;
    if (source != authorized->sourceRef) return nullptr;
    const auto *skill = dynamic_cast<const ViewAsSkillV2 *>(viewAs);
    if ((skill ? 2 : 1) != authorized->producerVersion) return nullptr;
    const auto reason = authorized->producerReason == CardUseStruct::CARD_USE_REASON_UNKNOWN
        ? request.reason : authorized->producerReason;
    const QString pattern = authorized->producerReason == CardUseStruct::CARD_USE_REASON_UNKNOWN
        ? request.pattern : authorized->producerPattern;
    ResponseNativeScope responseScope(m_room, player, reason, pattern);
    if ((authorized->activationRef.key.skillName == "PlusYiji" || authorized->activationRef.key.skillName == "fatewangzhe")
        && !authorizeAIStrategyPreviewPayment(request, m_room, player, authorized->activationRef,
            authorized->sourceRef, spec.subcardIds)) return nullptr;
    if(!aiRound45NotifiedSkill(request).isEmpty() && authorized->activationRef.key.skillName==aiRound45NotifiedSkill(request)
        && !authorizeAIRound45NotifiedPayment(request,m_room,player,authorized->activationRef,authorized->sourceRef,spec.subcardIds))return nullptr;
    const QString leiSkill=authorized->activationRef.key.skillName;
    if(request.kind==AIRequest::UseCard && request.pattern=="@@"+leiSkill
        && (leiSkill=="poxi"||leiSkill=="hongju"||leiSkill=="olhongju"||leiSkill=="mobilehongju")
        && !authorizeAIRound44LeiPayment(request,m_room,player,authorized->activationRef,authorized->sourceRef,spec.subcardIds))return nullptr;
    const Card *produced = nullptr;
    bool chumingPayment = false;
    const bool jincaiwangGenerated = skill && authorized->activationRef.key.skillName == "secondjincaiwang"
        && authorized->hasSelectedMaterials && authorized->selectedMaterialIds.size() == 1
        && authorized->subcardIds.isEmpty();
    if (jincaiwangGenerated && !round25JincaiwangMaterial(request, m_room, player,
            authorized->name, authorized->selectedMaterialIds)) return nullptr;
    if (skill) {
        ActiveSkillRequest activation;
        activation.reason = reason; activation.pattern = pattern; activation.initiator = player;
        activation.activationRef = authorized->activationRef; activation.sourceRef = authorized->sourceRef;
        activation.userString = authorized->declaredName;
        NotifiedPileQueryScope notifiedPile(activation, skill);
        if (!skill->canActivateRequest(activation)) return nullptr;
        if (authorized->costCount > 0 && !skill->hasIndependentAIConversion()) return nullptr;
        if (authorized->generatedHandPayment) {
            const QString generatedSkill=authorized->activationRef.key.skillName;
            if (generatedSkill == "lxtx_zhangwu") {
                if (!aiRound27ZhangwuGeneratedPayment(request, player, generatedSkill, spec.subcardIds)) return nullptr;
            } else if ((generatedSkill != "chopper" && generatedSkill != "tiaojiaoCMT")
                || (generatedSkill == "chopper" && spec.subcardIds.isEmpty())
                || (generatedSkill == "tiaojiaoCMT" && request.kind != AIRequest::Activate)
                || spec.subcardIds != player->handCards()) return nullptr;
        }
        if (authorized->hasSelectedMaterials && !jincaiwangGenerated) {
            const QJsonObject product{{"name", authorized->name}, {"skill", authorized->activationRef.key.skillName},
                {"suit", authorized->suit}, {"number", authorized->number}, {"subcards", responseIds(spec.subcardIds)}};
            chumingPayment = authorized->selectedMaterialIds.isEmpty()
                && authorizeAIRound24ChumingPayment(request, player, authorized->activationRef,
                    authorized->sourceRef, product);
            if (!chumingPayment && !aiAuditedNonconsumingSelection(request, player, authorized->activationRef.key.skillName,
                    product, responseIds(authorized->selectedMaterialIds))) return nullptr;
        }
        for (int id : (authorized->generatedHandPayment || jincaiwangGenerated) ? QList<int>()
                     : authorized->hasSelectedMaterials ? authorized->selectedMaterialIds : spec.subcardIds) {
            const Card *payment = Sanguosha->getCard(id);
            if (!payment || !skill->canSelectCard(activation, payment)) return nullptr;
            activation.selectedCardIds << id;
        }
        if (!skill->cardSelectionFeasible(activation)) return nullptr;
        SkillContext context;
        context.initiator = player; context.invoker = player; context.owner = player;
        context.activationRef = authorized->activationRef; context.sourceRef = source;
        context.instanceID = authorized->activationRef.key.instanceID;
        bool amountOk = false;
        context.amount = m_skillRuntime.getSkillInstanceAmount(skill->getAmountRef(context), &amountOk);
        if (!amountOk) context.amount = skill->getBaseAmount();
        if (!skill->isUsable(context)) return nullptr;
        produced = skill->createCard(activation);
    } else {
        const QJsonObject classPolicy = request.conversionPolicy.value("by_class").toObject()
            .value(authorized->policyClass).toObject();
        QString responsePattern = authorized->policyClass == "@skill_action" ? pattern : classPolicy.value("card_name").toString();
        if ((authorized->policyClass == "Peach" || authorized->policyClass == "Analeptic")
            && m_room.getCurrentDyingPlayer() == player) responsePattern = "peach+analeptic";
        if (reason == CardUseStruct::CARD_USE_REASON_PLAY) {
            if (request.kind != AIRequest::Activate || request.reason != reason
                || !viewAs->isEnabledAtPlay(player)) return nullptr;
        } else if (!viewAs->isEnabledAtResponse(player, responsePattern)) return nullptr;
        ResponseDeclarationScope declarationScope;
        if (!declarationScope.prepare(viewAs->objectName(), player, reason, pattern, authorized->activationRef,
            authorized->declaredName.isEmpty() ? authorized->name : authorized->declaredName,
            !authorized->declaredName.isEmpty())) return nullptr;
        QList<int> payable = player->handCards();
        for (const Card *card : player->getEquips()) if (card) payable << card->getEffectiveId();
        for (int id : player->getHandPile()) payable << id;
        for (int id : viewAs->getExpandPileCardIds(player)) payable << id;
        if (authorized->activationRef.key.skillName == "PlusYiji" || authorized->activationRef.key.skillName == "fatewangzhe") {
            if (!authorizeAIStrategyPreviewPayment(request, m_room, player, authorized->activationRef,
                    authorized->sourceRef, spec.subcardIds)) return nullptr;
            for (int id : spec.subcardIds) if (!payable.contains(id)) payable << id;
        }
        QList<const Card *> selected;
        for (int id : spec.subcardIds) {
            const Card *payment = Sanguosha->getCard(id);
            if (!payable.contains(id) || !payment || !viewAs->viewFilter(selected, payment)) return nullptr;
            selected << payment;
        }
        if (isAIRound33TongyiSource(request, authorized->activationRef.key.skillName, authorized->policyClass)) {
            produced = buildAIRound33TongyiSource(request, m_room, player,
                authorized->activationRef, authorized->sourceRef, authorized->policyClass,
                authorized->declaredName, spec.subcardIds);
        } else produced = viewAs->viewAs(selected);
        if (!declarationScope.retainProduced(produced)) return nullptr;
    }
    if (!produced) return nullptr;
    const auto reject = [&]() -> Card * {
        if (produced->isVirtualCard()) const_cast<Card *>(produced)->deleteLater();
        return nullptr;
    };
    const bool round33Physical = skill && aiRound33PlanningPhysicalScope(request,authorized->activationRef.key.skillName)
        && produced->isKindOf("EquipCard");
    if (round33Physical && (!authorizeAIRound33PlanningPhysicalProduct(request,m_room,player,
            authorized->activationRef,authorized->sourceRef,spec.subcardIds,produced)
        || authorized->legacyIdentityToken!=aiLegacyCardIdentity(m_room,player,produced))) return reject();
    const bool physicalProduct = round33Physical || (authorized->policyClass == "@skill_action" && skill
        && isAuditedPhysicalCardSkill(authorized->activationRef.key.skillName, reason, pattern));
    if (round33Physical) {
        // Final factory returned the same disclosed physical object/material/face.
    } else if (physicalProduct && isMarkedPhysicalCardSkill(authorized->activationRef.key.skillName, reason, pattern)) {
        bool printed = false; QJsonObject disclosed;
        const int id = produced->getEffectiveId();
        if (produced->isVirtualCard() || !spec.subcardIds.isEmpty()
            || !authorizeAIOlClanMarkedPhysicalProduct(request, id, &disclosed, &printed)
            || produced != (printed ? Sanguosha->getEngineCard(id) : Sanguosha->getCard(id))
            || (!printed && (produced->objectName() != disclosed.value("name").toString()
                || produced->getSuit() != disclosed.value("suit").toInt(-1)
                || produced->getNumber() != disclosed.value("number").toInt(-1)))) return reject();
    } else if (physicalProduct) {
        if (produced->isVirtualCard() || spec.subcardIds.size() != 1
            || produced != Sanguosha->getCard(spec.subcardIds.first())
            || produced->getEffectiveId() != spec.subcardIds.first()
            || !authorizeAIOlClanNotifiedEvaluation(request, spec.subcardIds.first())) return reject();
    } else if (jincaiwangGenerated) {
        if (!round25JincaiwangProduct(produced, authorized->name, authorized->selectedMaterialIds)) return reject();
    } else if (!produced->isVirtualCard() || produced->getSubcards() != spec.subcardIds) return reject();
    if (produced->objectName() != authorized->name || int(produced->getSuit()) != authorized->suit
        || produced->getNumber() != authorized->number) return reject();
    if (authorized->skinGeneratedDeclaration) {
        std::unique_ptr<SkillCard> sourcePolicy(LuaSkillCard::Parse("#skin:.:") );
        if (!authorized->declaredName.isEmpty() || authorized->policyClass != "@skill_action"
            || authorized->producerVersion != 2 || !authorized->subcardIds.isEmpty()
            || !aiRound44SkinProduct(request, player, authorized->activationRef, authorized->sourceRef,
                produced, sourcePolicy.get())) return reject();
        const auto *proxy = static_cast<const SkillCard *>(produced);
        if (proxy->getUserString() != authorized->skinGeneratedUserString) return reject();
    } else if (const auto *proxy = dynamic_cast<const SkillCard *>(produced))
        if (proxy->getUserString() != authorized->declaredName) return reject();
    if (authorized->activationRef.key.skillName == "tyfenwu" && authorized->skillName == "_tyfenwu"
        && !aiRound53TyfenwuRawProduct(request,player,authorized->activationRef.key.skillName,produced)) return reject();
    Card *card = const_cast<Card *>(produced);
    if (!physicalProduct) {
        card->setActivationSkill(authorized->activationRef.key.skillName, authorized->activationRef.key.instanceID);
        card->setSourceSkill(authorized->sourceRef.key.skillName, authorized->sourceRef.key.instanceID);
    }
    // The issuer already applied cardsView's request attribution. Direct Guhuo
    // and per-card view_as retain the native producer's original skill name.
    if (!physicalProduct) card->setSkillName(authorized->skillName);
    QString actualPattern = request.pattern;
    if (actualPattern.endsWith('!')) actualPattern.chop(1);
    AIRound47XufaSourceCard xufaRebuilt(m_room,request,player,card,authorized->activationRef.key.skillName);
    ResponseNativeScope actualScope(m_room, player, request.reason, request.pattern);
    const auto matchesActual = [&](const Card *candidate) {
        return request.kind == AIRequest::RespondCard
            ? Sanguosha->matchPattern(actualPattern, player, candidate)
            : Sanguosha->matchExpPattern(actualPattern, player, candidate);
    };
    const bool classPlanning = usesClassResponsePlanning(request);
    const QJsonObject planningValue = classPlanning ? authorized->responseClassPlanning : authorized->responsePlanning;
    if (!planningValue.isEmpty()) {
        if (classPlanning && !planningValue.value("exists").toBool()) return reject();
        const QString name = planningValue.value("card").toObject().value("name").toString();
        Card *planning = Sanguosha->cloneCard(name);
        if (!planning) return reject();
        if (!classPlanning) planning->setSkillName(authorized->skillName);
        if (classPlanning) {
            for (const auto &id : planningValue.value("card").toObject().value("subcards").toArray())
                planning->addSubcard(id.toInt());
        } else if (!card->willThrow()) planning->addSubcards(card->getSubcards());
        const bool accepted = !player->isLocked(planning)
            && !player->isCardLimited(planning, request.handlingMethod)
            && matchesActual(planning);
        planning->deleteLater();
        if (!accepted) return reject();
    } else if (authorized->policyClass == "@skill_action" || chumingPayment) {
        // The issued V2 scope is this exact native activation request. Patterns
        // such as @@qixing name an activation, not a printed card expression.
        if (reason != request.reason || pattern != request.pattern
            || player->isCardLimited(card, request.handlingMethod)) return reject();
    } else if ((request.kind == AIRequest::Activate ? !card->isAvailable(player)
                : !matchesActual(card))
               || player->isCardLimited(card, request.handlingMethod)) return reject();
    if (!aiRound44RangeRequestCurrent(m_room,request)) return reject();
    return card;

}

bool AiDecisionCoordinator::applyResult(ServerPlayer *player, const AIRequest &request,
                                        const AIResult &result,
                                        CardUseStruct &cardUse) const
{
    if (request.worldView.strategyFacts.contains("readonly_play_context")) {
        if (!aiRound28NiwoResponseQuestion(request,m_room,player)
            || request.worldView.strategyFacts.value("readonly_play_context").toObject().value("source")!="OverseasVersion.ov_niwo"
            || request.readonlyParentConversionPolicy.isEmpty()
            || (result.kind!=AIResult::Pass && (result.kind!=AIResult::UseCard
                || result.action.cardSpec.name!="ov_niwoCard"))) return false;
    }
    if (!player || !result.handled || !result.errorCode.isEmpty()
        || result.decisionId != request.decisionId
        || result.stateRevision != request.stateRevision)
        return false;
    if (!aiRound44RangeRequestCurrent(m_room,request)) return false;

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

    if(result.action.hasCardSpec && hasAIRound53DiouReceipt(request,result.action.cardSpec.conversionId)) {
        EngineRuntimeContextScope engine(*Sanguosha,&m_room);LuaRuntime::Binding game(m_room.roomRuntime()->lua(),false);
        ResponseNativeScope native(m_room,player,request.reason,request.pattern);CardUseStruct selected;
        if(!applyAIRound53DiouReceipt(request,player,result.action.cardSpec,result.action.selectedTargetNames,
            [&](CardUseStruct&use){return m_skillRuntime.resolveCardSkillInstance(use);},selected))return false;
        selected.m_validateTargets=true;
        if(player->isCardLimited(selected.card,selected.card->getHandlingMethod())||!m_room.areCardTargetsLegal(selected))return false;
        if(!aiRound44RangeRequestCurrent(m_room,request))return false;cardUse=selected;return true;
    }
    if(result.action.hasCardSpec && hasAIRound53XuanzhuReceipt(request,result.action.cardSpec.conversionId)) {
        EngineRuntimeContextScope engine(*Sanguosha,&m_room);LuaRuntime::Binding game(m_room.roomRuntime()->lua(),false);
        ResponseNativeScope native(m_room,player,request.reason,request.pattern);
        CardUseStruct selected;
        if(!applyAIRound53XuanzhuReceipt(request,player,result.action.cardSpec,result.action.selectedTargetNames,
            [&](CardUseStruct &use){return m_skillRuntime.resolveCardSkillInstance(use);},selected))return false;
        selected.m_validateTargets=true;
        if(player->isCardLimited(selected.card,selected.card->getHandlingMethod())||!m_room.areCardTargetsLegal(selected))return false;
        if(!aiRound44RangeRequestCurrent(m_room,request))return false;
        cardUse=selected;return true;
    }
    if(result.action.hasCardSpec && hasAIRound53XufaReceipt(request,result.action.cardSpec.conversionId)) {
        EngineRuntimeContextScope engine(*Sanguosha,&m_room);LuaRuntime::Binding game(m_room.roomRuntime()->lua(),false);
        ResponseNativeScope native(m_room,player,request.reason,request.pattern);
        CardUseStruct selected;
        if(!applyAIRound53XufaReceipt(request,player,result.action.cardSpec,result.action.selectedTargetNames,
            [&](CardUseStruct &use){return m_skillRuntime.resolveCardSkillInstance(use);},selected))return false;
        selected.m_validateTargets=true;
        if(player->isCardLimited(selected.card,selected.card->getHandlingMethod())||!m_room.areCardTargetsLegal(selected))return false;
        if(!aiRound44RangeRequestCurrent(m_room,request))return false;
        cardUse=selected;return true;
    }
    if(result.action.hasCardSpec && hasAIRound47HeqiaReceipt(request,result.action.cardSpec.conversionId)) {
        EngineRuntimeContextScope engine(*Sanguosha,&m_room);LuaRuntime::Binding game(m_room.roomRuntime()->lua(),false);
        ResponseNativeScope native(m_room,player,request.reason,request.pattern);
        CardUseStruct selected;
        if(!applyAIRound47HeqiaReceipt(request,player,result.action.cardSpec,result.action.selectedTargetNames,
            [&](CardUseStruct &use){return m_skillRuntime.resolveCardSkillInstance(use);},selected))return false;
        selected.m_validateTargets=true;
        if(player->isCardLimited(selected.card,request.handlingMethod) || !m_room.areCardTargetsLegal(selected))return false;
        if(!aiRound44RangeRequestCurrent(m_room,request))return false;
        cardUse=selected;return true;
    }
    if(result.action.hasCardSpec && hasAIRound47BLeiluanReceipt(request,result.action.cardSpec.conversionId)) {
        EngineRuntimeContextScope engine(*Sanguosha,&m_room);LuaRuntime::Binding game(m_room.roomRuntime()->lua(),false);
        ResponseNativeScope native(m_room,player,request.reason,request.pattern);
        CardUseStruct selected;
        if(!applyAIRound47BLeiluanReceipt(request,player,result.action.cardSpec,result.action.selectedTargetNames,
            [&](CardUseStruct &use){return m_skillRuntime.resolveCardSkillInstance(use);},selected))return false;
        selected.m_validateTargets=true;
        if(player->isCardLimited(selected.card,request.handlingMethod) || !m_room.areCardTargetsLegal(selected))return false;
        if(!aiRound44RangeRequestCurrent(m_room,request))return false;
        cardUse=selected;return true;
    }
    if(result.action.hasCardSpec && hasAIRound51NotifiedCosts(request,result.action.cardSpec.conversionId)) {
        EngineRuntimeContextScope engine(*Sanguosha,&m_room);LuaRuntime::Binding game(m_room.roomRuntime()->lua(),false);
        ResponseNativeScope native(m_room,player,request.reason,request.pattern);
        CardUseStruct selected;
        if(!applyAIRound51NotifiedCosts(request,player,result.action.cardSpec,result.action.selectedTargetNames,
            [&](CardUseStruct &use){return m_skillRuntime.resolveCardSkillInstance(use);},selected))return false;
        selected.m_validateTargets=true;
        // Original Room::resolveCardUse checks the parsed card's MethodNone,
        // not the prompt's MethodUse; preserve the actual raw SkillCard ABI.
        if(player->isCardLimited(selected.card,selected.card->getHandlingMethod()) || !m_room.areCardTargetsLegal(selected))return false;
        if(!aiRound44RangeRequestCurrent(m_room,request))return false;
        cardUse=selected;return true;
    }
    if(result.action.hasCardSpec && hasAIRound46JianmanReceipt(request,result.action.cardSpec.conversionId)) {
        EngineRuntimeContextScope engine(*Sanguosha,&m_room);LuaRuntime::Binding game(m_room.roomRuntime()->lua(),false);
        ResponseNativeScope native(m_room,player,request.reason,request.pattern);
        CardUseStruct selected;
        if(!applyAIRound46JianmanReceipt(request,player,result.action.cardSpec,result.action.selectedTargetNames,
            [&](CardUseStruct &use){return m_skillRuntime.resolveCardSkillInstance(use);},selected))return false;
        selected.m_validateTargets=true;
        if(player->isCardLimited(selected.card,request.handlingMethod) || !m_room.areCardTargetsLegal(selected))return false;
        if(!aiRound44RangeRequestCurrent(m_room,request))return false;
        cardUse=selected;return true;
    }
    if(result.action.hasCardSpec && hasAIRound41FaqiReceipt(request,result.action.cardSpec.conversionId)) {
        EngineRuntimeContextScope engine(*Sanguosha,&m_room);LuaRuntime::Binding game(m_room.roomRuntime()->lua(),false);
        ResponseNativeScope native(m_room,player,request.reason,request.pattern);
        CardUseStruct selected;
        if(!applyAIRound41FaqiReceipt(request,player,result.action.cardSpec,result.action.selectedTargetNames,
            [&](CardUseStruct &use){return m_skillRuntime.resolveCardSkillInstance(use);},selected))return false;
        selected.m_validateTargets=true;
        if(player->isCardLimited(selected.card,request.handlingMethod) || !m_room.areCardTargetsLegal(selected))return false;
        if(!aiRound44RangeRequestCurrent(m_room,request))return false;
        cardUse=selected;return true;
    }
    QSet<QString> targetNames;
    QMap<QString, int> targetVotes;
    const AICardConversionView *voteAuthority = result.action.hasCardSpec
        ? responseTicket(request, result.action.cardSpec.conversionId) : nullptr;
    foreach (const QString &targetName, result.action.selectedTargetNames) {
        const int votes = ++targetVotes[targetName];
        if (targetNames.contains(targetName)
            && (!voteAuthority || voteAuthority->maxVotes.value(targetName, 0) < votes)) return false;
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
        bool previewBorrow = false;
        if (request.conversionLedger && request.conversionLedger->previewPhysicalReceipts.contains(useCardId)) {
            const auto receipt = request.conversionLedger->previewPhysicalReceipts.value(useCardId);
            previewBorrow = authorizeAIStrategyPreviewPhysicalProduct(request, m_room, player,
                receipt.activationRef, receipt.sourceRef, useCardId)
                || authorizeAIRound45NotifiedPhysical(request,m_room,player,receipt.activationRef,receipt.sourceRef,useCardId)
                || authorizeAIRound49NotifiedPhysical(request,m_room,player,receipt.activationRef,receipt.sourceRef,useCardId);
        }
        const bool danlvePhysical=authorizeAIRound45DanlvePhysical(m_room,request,player,useCardId);
        const Card *played = (owned || previewBorrow || danlvePhysical || liveTaoxiBorrow(m_room, player, request.worldView, useCardId))
            ? Sanguosha->getCard(useCardId) : nullptr;
        if (!played) return false;
        // Owning the card is not the same as having been offered it for this question.
        // A request that never listed this card as a candidate never authorized it, so
        // a candidate list that does carry one must be matched against.
        if (!previewBorrow && !danlvePhysical && !request.cardCandidates.isEmpty()) {
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
        const AICardConversionView *authorized = responseTicket(request, result.action.cardSpec.conversionId);
        if (!authorized) {
            if (built->isVirtualCard()) built->deleteLater();
            return false;
        }
        candidate.hasSkillActivationRequest = authorized->producerVersion == 2;
        candidate.activationRef = authorized->activationRef;
        candidate.sourceRef = authorized->sourceRef;
        if (built->isVirtualCard()) candidate.setOwnedCard(built);
        else candidate.card = built;
        if (request.kind==AIRequest::UseCard && request.pattern=="@fatewangzhe"
            && authorized->activationRef.key.skillName=="fatewangzhe"
            && !AIRound47FateUseAccess::attach(request,m_room,player,*authorized,candidate)) return false;
        if (aiRound33PlanningPhysicalScope(request,authorized->activationRef.key.skillName)
            && built->isKindOf("EquipCard")
            && !AIRound33PhysicalUseAccess::attach(request,m_room,player,*authorized,candidate)) return false;
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
    // R45's two notified V2 sources return the original physical card. Their
    // selected-ID result must not travel through ActiveSkillCard::changeCard,
    // which transfers activation metadata/tags to its newly created product.
    // Only an actually issued same-request physical receipt admits this path.
    const QString round45PhysicalSkill = aiRound45NotifiedSkill(request);
    if (round45PhysicalSkill == "keolzhuri" || round45PhysicalSkill == "keolsaoguslash") {
        if (request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            || request.handlingMethod != Card::MethodUse
            || !request.hasSkillActionContext || !request.conversionLedger
            || result.action.selectedCardIds.size() != 1) return false;
        const int id = result.action.selectedCardIds.first();
        const auto receipt = request.conversionLedger->previewPhysicalReceipts.constFind(id);
        if (receipt == request.conversionLedger->previewPhysicalReceipts.cend()
            || receipt->activationRef != context.activationRef
            || receipt->sourceRef != context.sourceRef) return false;
        EngineRuntimeContextScope engine(*Sanguosha, &m_room);
        LuaRuntime::Binding game(m_room.roomRuntime()->lua(), false);
        ResponseNativeScope native(m_room, player, request.reason, request.pattern);
        if (!authorizeAIRound45NotifiedPhysical(request, m_room, player,
                context.activationRef, context.sourceRef, id)) return false;
        const Card *physical = m_room.getCard(id);
        if (!physical || physical->isVirtualCard()
            || player->isCardLimited(physical, request.handlingMethod)) return false;
        candidate.card = physical;
        candidate.hasSkillActivationRequest = true;
        candidate.activationRef = context.activationRef;
        candidate.sourceRef = context.sourceRef;
        // Room's audited physical resolver rechecks the native selection and
        // product while leaving the physical card's metadata and ownership alone.
        cardUse = candidate;
        return true;
    }
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

void AiDecisionCoordinator::projectReadonlyOlClanPlanning(ServerPlayer *player, AIRequest &request) const
{
    QJsonObject context = aiRound23OlClanPlanningContext(request, m_room, player);
    if (context.isEmpty()) context = aiRound27ChoicePlanningContext(request, m_room, player);
    if (context.isEmpty()) context = aiRound28ChoicePlanningContext(request, m_room, player);
    if (context.isEmpty()) return;
    EngineRuntimeContextScope engineScope(*Sanguosha, &m_room);
    LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
    ResponseNativeScope responseScope(m_room, player, CardUseStruct::CARD_USE_REASON_PLAY, QString());
    if (aiRound28NiwoResponseQuestion(request,m_room,player)) {
        request.readonlyParentConversionPolicy=request.conversionPolicy;
        request.readonlyParentSkillActions=request.skillActions;
    }
    AIRequest planning = request;
    planning.kind = AIRequest::Activate;
    planning.reason = CardUseStruct::CARD_USE_REASON_PLAY;
    planning.pattern.clear(); planning.prompt.clear(); planning.handlingMethod = Card::MethodUse;
    planning.skillActions.clear(); planning.cardCandidates.clear(); planning.cardConversions.clear();
    for (const SkillInstance &instance : player->getSkillInstances()) {
        AiSkillActionContext action;
        if (buildSkillActionContext(player, instance, planning.reason, planning.pattern, action))
            planning.skillActions << action;
    }
    int budget = 16384;
    QSet<int> seen;
    const auto append = [&](const Card *card) {
        if (!card || seen.contains(card->getEffectiveId())) return;
        seen.insert(card->getEffectiveId());
        planning.cardCandidates << makeAICardCandidate(m_room, player, card,
            Card::MethodUse, AIRequest::Activate, QString(), budget);
    };
    for (const Card *card : player->getHandcards()) append(card);
    for (const Card *card : player->getEquips()) append(card);
    // addHandPile is the source enumeration domain. A native pile ID alone
    // does not authorize a current face: require the owned disclosed snapshot.
    for (int id : player->getHandPile()) {
        bool disclosed = false;
        for (const AICardPileView &pile : planning.worldView.self.piles)
            for (const AICardView &face : pile.cards) if (face.effectiveId == id) disclosed = true;
        if (disclosed && m_room.getCardOwner(id) == player) append(Sanguosha->getCard(id));
    }
    const int borrowedId = planning.worldView.strategyFacts.value("yjcm2015").toObject()
        .value("taoxi_borrowed").toObject().value("card_id").toInt(-1);
    if (liveTaoxiBorrow(m_room, player, planning.worldView, borrowedId))
        append(Sanguosha->getCard(borrowedId));
    for (int i = 0; i < planning.cardCandidates.size(); ++i) planning.cardCandidates[i].candidateId = i + 1;
    buildCardConversions(player, planning, budget, true);
    // Existing per-candidate history and lawful random-hand query metadata are
    // needed by full turn planning too. They remain bound to this same parent
    // decision/revision; the hidden IDs stay solely in native randomCardChoices.
    projectAIStrategyRequest(planning, m_room, player);
    // Omitted prototypes may only use this explicitly audited source mapping.
    // Current OlClan getTurnUse source adapters all pass their prototype; these
    // entries also support the older same-source no-argument binder ABI.
    static const QMap<QString, QString> prototypes{
        {"zuguangu", "#zuguanguCard"}, {"zuyirong", "#zuyirongCard"},
        {"kezuyunshen", "#kezuyunshenCard"}, {"kezulieshi", "#kezulieshiCard"},
        {"kezubolong", "#kezubolongCard"}, {"zukaiji", "#zukaijiCard"},
        {"zutanfeng", "#zutanfengCard"}, {"zulianzhuvs", "#zulianzhuCard"},
        {"kezufuxun", "#kezufuxunCard"}, {"kezuxumin", "#kezuxuminCard"},
        {"kezumingjie", "#kezumingjieCard"}, {"kezuqiuxin", "#kezuqiuxinCard"}};
    QJsonArray actions = planning.conversionPolicy.value("skill_actions").toArray();
    for (int i = 0; i < actions.size(); ++i) {
        QJsonObject row = actions.at(i).toObject();
        const QString prototype = prototypes.value(row.value("skill_name").toString());
        if (!prototype.isEmpty()) row.insert("readonly_prototype", prototype);
        actions.replace(i, row);
    }
    planning.conversionPolicy.insert("skill_actions", actions);
    request.worldView = planning.worldView;
    request.choiceOptions.context = planning.choiceOptions.context;
    request.randomCardChoices = planning.randomCardChoices;
    request.cardCandidates = planning.cardCandidates;
    request.skillActions = planning.skillActions;
    request.conversionPolicy = planning.conversionPolicy;
    request.cardConversions.clear(); request.conversionsEnumerated = false;
    request.stagedActionsComplete = false;
    request.worldView.strategyFacts.insert("readonly_play_context", context);
}

AIRequest AiDecisionCoordinator::makeChoiceRequest(ServerPlayer *player,
                                                   AIRequest::DecisionKind kind,
                                                   const AIChoiceOptions &options) const
{
    AIRequest request = makeRequest(player, kind, CardUseStruct::CARD_USE_REASON_UNKNOWN,
                                    QString(), QString(), Card::MethodNone);
    request.choiceOptions = options;
    // Duyi's current AG notification is visible to its actual recipient.
    // The ledger preserves only notified faces, never the current hidden deck.
    if (kind == AIRequest::PlayerChosen && options.reason == "duyi"
        && !m_room.getTag("DuyiPendingEffects").toList().isEmpty()) {
        const auto cards = readAIStrategyNotifiedPile(m_room, player, "#ai_current_ag").value("cards").toObject();
        if (cards.size() == 1) request.choiceOptions.context.insert("duyi_card", cards.begin().value());
    }
    if (kind == AIRequest::Choice && options.reason == "eastfast") {
        const auto cards = readAIStrategyNotifiedPile(m_room, player, "#ai_current_ag").value("cards").toObject();
        const QString id = QString::number(player->getMark("eastfast_card"));
        QJsonObject disclosed{{"known", cards.contains(id)}, {"draw_pile_count", m_room.getDrawPile().size()}};
        if (cards.contains(id)) disclosed.insert("card", cards.value(id));
        request.choiceOptions.context.insert("eastfast", disclosed);
    }
    // These questions reveal their offered cards to this viewer. CardChosen is
    // deliberately excluded: another player's hidden hand is count-only.
    if (kind == AIRequest::Discard || kind == AIRequest::AmazingGrace
        || kind == AIRequest::Yiji || kind == AIRequest::Guanxing) {
        request.choiceOptions.candidatesComplete = true;
        for (int cardId : options.cardIds) {
            const Card *card = Sanguosha->getCard(cardId);
            if (card) request.choiceOptions.cards << makeAICardView(card,
                kind == AIRequest::AmazingGrace && (options.reason == "kezumingjie" || options.reason == "kezulilun" || options.reason == "gengdu" || options.reason == "zhuning"));
            else request.choiceOptions.candidatesComplete = false;
        }
    }
    projectAIStrategyRequest(request, m_room, player);
    projectAIRound39Wendao(request, m_room, player);
    projectAIRound40TargetHistory(request, m_room, player);
    projectAIStrategyResponseContext(request, m_room, player, QVariant(), request.choiceOptions.context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIStrategyRound24Response(request, m_room, player, QVariant(), request.choiceOptions.context);
    projectAIStrategyRetrialContext(request, m_room, player, QVariant(), request.choiceOptions.context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIStrategyChoiceContext(request, m_room, player, QVariant(), request.choiceOptions.context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIConstructionContext(request, m_room, player,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound21PlanningContext(request, m_room, player,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound24FeiDIYContext(request, m_room, player,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound24ChoiceContext(request, m_room, player);
    projectAIRound25ChoiceContext(request, m_room, player, QVariant(), request.choiceOptions.context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound26ChoiceContext(request, m_room, player, QVariant(), request.choiceOptions.context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound27ChoiceContext(request, m_room, player, QVariant(), request.choiceOptions.context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIStrategyRound25Response(request, m_room, player, request.choiceOptions.context);
    projectAIRound25OriginalContext(request, m_room, player, QVariant(),
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound26OriginalContext(request, m_room, player, QVariant(),
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound26PlanningContext(request, m_room, player, QVariant(), request.choiceOptions.context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound28PlanningContext(request, m_room, player, QVariant(), request.choiceOptions.context,
        [](const Card *card) { return publicDecisionCard(card); },
        [](const Card *card, int, const QString &) { return publicDecisionCard(card); });
    projectAIRound27PlanningContext(request, m_room, player, QVariant(), request.choiceOptions.context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound21ChoiceContext(request, m_room, player, request.choiceOptions.context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound22OlClanPlanContext(request, m_room, player);
    projectAIRound22ChoiceContext(request, m_room, player, request.choiceOptions.context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIStrategyRound22OlClanObservations(request, m_room, player, QVariant(), request.choiceOptions.context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIStrategyYCZH2017(request, m_room, player, [&](const Card *card) {
        return publicDecisionCard(card, request.choiceOptions.reason != "funan");
    });
    if (kind == AIRequest::Discard && options.reason == "enyuan") {
        const QVariant damage = player->getTag("enyuan_data");
        if (damage.canConvert<DamageStruct>()) projectDecisionContext(player, damage, request);
    }
    if (kind == AIRequest::Discard && options.reason == "bunkasai")
        projectDecisionContext(player, QVariant(), request);
    projectAIRound24OriginalContext(request, m_room, player, QVariant(),
        [](const Card *card) { return publicDecisionCard(card); });
    if ((kind == AIRequest::PlayersChosen && (options.reason == "mobilebenxi" || options.reason == "fanyin"))
        || (kind == AIRequest::PlayerChosen && (options.reason == "jincanmou" || options.reason == "yb_zhuzhan2" || options.reason == "yb_fujia2" || options.reason == "ov_zenhui" || options.reason == "ov_qirang" || options.reason == "ov_yizhu" || options.reason == "ov_ronggui" || options.reason == "ov_lihuo"))
        || (kind == AIRequest::Discard && options.reason == "yb_zhuzhan2"))
        projectDecisionContext(player, QVariant(), request);
    projectReadonlyOlClanPlanning(player, request);
    if (request.choiceOptions.context.contains("round26_choice"))
        request.worldView.decisionContext.insert("round26_choice", request.choiceOptions.context.value("round26_choice"));
    if (request.choiceOptions.context.contains("round27_choice"))
        request.worldView.decisionContext.insert("round27_choice", request.choiceOptions.context.value("round27_choice"));
    request.worldView.strategyFacts.insert("original_guanxing_flag_write",
        request.kind==AIRequest::Guanxing && aiRound35OriginalQuestion(request));
    request.worldView.strategyFacts.insert("round25_cixiao_speech",
        request.kind == AIRequest::PlayerChosen && request.choiceOptions.reason == "cixiao"
        && player && player->hasSkill("cixiao"));
    // R53: makeRequest runs before these choice options exist. Reproject only
    // the existing finite owner property after the actual Choulve question is known.
    if (request.kind == AIRequest::PlayerChosen && request.choiceOptions.reason == "choulve"
        && request.choiceOptions.question == "askForPlayerChosen")
        projectAIRound49OwnerNames(request, m_room, player);
    projectAIRound42Liehou(request, m_room, player);
    projectAIRound43Qingsi(request, m_room, player);
    projectAIRound47EQinyin(request, m_room, player);
    projectAIRound49ChoiceProperties(request, m_room, player);
    projectAIRound49Yitian(request, m_room, player);
    projectAIRound49Bozhi(request,m_room,player);
    projectAIRound49MaotuYinhu(request,m_room,player);
    projectAIRound49hPlanning(request,m_room,player);
    projectAIRound49nPlanning(request,m_room,player);
    projectAIRound49pPlanning(request,m_room,player);
    projectAIRound49oPlanning(request,m_room,player);
    projectAIRound49gPlanning(request,m_room,player);
    projectAIRound49fPlanning(request,m_room,player);
    projectAIRound49ePlanning(request,m_room,player);
    projectAIRound49dPlanning(request,m_room,player);
    projectAIRound49cPlanning(request,m_room,player);
    projectAIRound50YuriQuestion(request,m_room,player,[](const Card*card){return publicDecisionCard(card);});
    projectAIRound50Planning(request,m_room,player);
    projectAIRound51Collateral(request,m_room,player);
    projectAIRound53Collateral(request,m_room,player);
    projectAIRound52Rangyi(request,m_room,player);
    projectAIRound45Newgenerals(request,m_room,player,[](const Card *card){return publicDecisionCard(card);});
    projectAIRound50HTMSEvents(request,m_room,player,[](const Card *card){return publicDecisionCard(card);});
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

// Opt-in, payload-free evidence. A proposed answer is deliberately distinct from
// native acceptance and from a fallback; logs never contain hand IDs or prompts.
static void traceIsolatedDecision(const AIRequest &request, const AIResult &result,
                                  const char *stage)
{
    if (!Config.value(QStringLiteral("AiDecisionTrace"), false).toBool()) return;
    const char *outcome = !result.errorCode.isEmpty()
        ? (result.errorCode == QStringLiteral("AI_UNSUPPORTED") ? "unsupported"
           : result.errorCode == QStringLiteral("AI_NATIVE_REJECTED") ? "native-rejected" : "error")
        : !result.handled ? "unhandled" : result.kind == AIResult::Pass ? "pass" : "answer";
    qInfo() << "[ISOLATED_AI]" << "decision" << request.decisionId
            << "revision" << request.stateRevision << "kind" << int(request.kind)
            << "stage" << stage << "outcome" << outcome << "error" << result.errorCode
            << "estimate_count" << result.knowledgeEstimates.size();
    if (qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")) {
        // Existing diagnostic reason labels only: never values, card IDs or
        // selected private payloads. This grants no strategy/source authority.
        for (const auto &entry : result.knowledgeEstimates) {
            const auto row = entry.toObject();
            const QJsonObject labels{{"context", row.value("context").toString()},
                                     {"basis", row.value("basis").toString()}};
            std::fprintf(stderr, "R56_ESTIMATE_LABELS %s\n",
                QJsonDocument(labels).toJson(QJsonDocument::Compact).constData());
        }
        std::fflush(stderr);
    }
}

bool AiDecisionCoordinator::externalAnswer(ServerPlayer *player, const AIRequest &request,
    AIResult &result, const std::function<bool(const AIResult &)> &validate) const
{
    auto endpoint = m_room.externalAgent(player->objectName());
    if (!endpoint) return false;
    invalidateAIRound29ChoicePriorities(m_room);
    invalidateAIRound30TargetCache(m_room);
    invalidateAIRound30TargetRevises(m_room);
    invalidateAIRound44RangeDecision(m_room);
    interruptAIChoiceContinuity(m_room);
    endpoint->begin(request);
    while (true) {
        const auto outcome = endpoint->awaitReply(result);
        if (outcome == ExternalAgentEndpoint::Fallback) {
            endpoint->finish();
            qInfo() << "[EXTERNAL_AGENT] disconnect SmartAI fallback" << player->objectName()
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
            qInfo() << "[EXTERNAL_AGENT] explicit local SmartAI route" << player->objectName()
                    << "decision" << request.decisionId << "kind" << int(request.kind)
                    << "revision" << request.stateRevision;
            return false; // the existing caller executes its native legacy callback once
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
                                      AIResult &result, bool *fromIsolated) const
{
    if (fromIsolated)
        *fromIsolated = false;
    if (!player || !player->getAI()) return false;
    if (externalAnswer(player, request, result, [&](const AIResult &answer) {
            if (!answer.handled || !answer.errorCode.isEmpty() || answer.decisionId != request.decisionId
                || answer.stateRevision != request.stateRevision
                || !aiRound44RangeRequestCurrent(m_room,request)) return false;
            AIResult resolved = answer;
            if (!resolveAIStrategyRandomChoice(m_room, player, request, resolved)) return false;
            result = resolved;
            return true;
        })) {
        if (fromIsolated) *fromIsolated = true; // strict candidate/count gates
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
        std::unique_ptr<AIRound44RangeRequestAlias> rangeHintAlias;
        if(deferDefault)rangeHintAlias.reset(new AIRound44RangeRequestAlias(request,hinted));
        result = m_room.roomRuntime()->ai().decideIsolated(deferDefault ? hinted : request);
        if (result.handled && result.errorCode.isEmpty()
            && result.decisionId == request.decisionId && result.stateRevision == request.stateRevision
            && aiRound44RangeRequestCurrent(m_room,request,player)
            && !resolveAIStrategyRandomChoice(m_room, player, request, result)) {
            result.handled = false;
            result.errorCode = QStringLiteral("AI_NATIVE_REJECTED");
        }
        traceIsolatedDecision(request, result, "proposed-value");
        // An isolated answer counts only while it still belongs to this decision and
        // this board state. A stale one is dropped and the legacy AI answers instead;
        // the stamp is never rewritten to make an old answer acceptable.
        isolatedAnswer = result.handled && result.errorCode.isEmpty()
            && result.kind != AIResult::UseCard
            && result.decisionId == request.decisionId
            && result.stateRevision == request.stateRevision
            && aiRound44RangeRequestCurrent(m_room,request,player);
        if (!isolatedAnswer) {
            traceIsolatedDecision(request, result, "fallback");
            result = legacy(request);
        }
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
    if (!viewer || (!Config.EnableAI && !m_room.externalAgent(viewer->objectName()))) return;
    EngineRuntimeContextScope scope(*Sanguosha, &m_room);
    LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
    AIIdentityProjectionScope identityScope(m_room, viewer);
    QJsonObject &context = request.choiceOptions.context;
    projectAIRound47OlClanScalar(request,m_room,viewer,data,context);
    projectAIRound49ExtraPlanningContext(request,data);
    projectAIRound53Mingfa(request,data);
    projectAIRound53Lianzhan(request,m_room,viewer,data);
    if (request.kind == AIRequest::Choice && request.choiceOptions.reason == "nyarz_yuheng"
        && data.metaType().id() == QMetaType::Int)
        context.insert("nyarz_yuheng_count", data.toInt());
    QJsonObject yingbian;
    const QString question = request.choiceOptions.reason;
    static const QMap<QString, QString> yingbianTags{
        {"yb_fujia2", "yb_fujia2_data"}, {"yb_zhuzhan", "yb_zhuzhan_data"},
        {"yb_zhuzhan2", "yb_zhuzhan2_data"}, {"yb_kongchao3", "yb_kongchao3_data"},
        {"yb_canqu2", "yb_canqu2_data"}};
    if (yingbianTags.contains(question)
        && (request.kind == AIRequest::PlayerChosen || request.kind == AIRequest::Discard)) {
        const QVariant value = viewer->getTag(yingbianTags.value(question));
        if (!value.isValid()) yingbian.insert("use", false);
        else if (value.canConvert<CardUseStruct>()) {
            const auto use = value.value<CardUseStruct>();
            QJsonArray targets;
            for (const auto *target : use.to)
                if (target && target->getRoom() == &m_room) targets.append(target->objectName());
            QJsonValue face(false);
            if (use.card) {
                auto row = publicDecisionCard(use.card);
                row.insert("evaluation_only", true);
                row.insert("evaluation_owner", viewer->objectName());
                row.insert("evaluation_rules", responseOwnerRules(m_room, viewer, use.card));
                row.insert("target_residue", responseResidueJson(responseResidue(m_room, viewer, use.card)));
                int budget = 4096;
                const auto candidate = makeAICardCandidate(m_room, viewer, use.card,
                    Card::MethodUse, AIRequest::Activate, QString(), budget);
                row.insert("evaluation_candidate", responseCandidateJson(candidate));
                QJsonObject sourceRules;
                if (use.from && use.from->getRoom() == &m_room) {
                    QJsonObject values;
                    const auto facts = projectAICanUseTargets(m_room, use.from, use.card);
                    for (auto it = facts.cbegin(); it != facts.cend(); ++it) values.insert(it.key(), it.value());
                    sourceRules.insert(use.from->objectName(), values);
                }
                row.insert("can_use_by_player", sourceRules);
                face = row;
            }
            yingbian.insert("use", QJsonObject{{"card", face},
                {"from", use.from && use.from->getRoom() == &m_room ? QJsonValue(use.from->objectName()) : QJsonValue(false)},
                {"to", targets}});
        }
    }
    if (request.kind == AIRequest::SkillInvoke && question == "IceSlash") {
        const QVariant value = viewer->getTag("IceDamageData");
        if (!value.isValid()) yingbian.insert("ice_damage", false);
        else if (value.canConvert<DamageStruct>()) {
            const auto damage = value.value<DamageStruct>();
            if (damage.to && damage.to->getRoom() == &m_room)
                yingbian.insert("ice_damage", QJsonObject{{"from", damage.from ? QJsonValue(damage.from->objectName()) : QJsonValue(false)},
                    {"to", damage.to->objectName()}, {"damage", damage.damage}, {"nature", int(damage.nature)},
                    {"chain", damage.chain}, {"transfer", damage.transfer}, {"by_user", damage.by_user},
                    {"card", damage.card ? QJsonValue(publicDecisionCard(damage.card)) : QJsonValue(false)}});
        }
    }
    // The third isCardLimited argument has distinct native hand-pattern
    // semantics. Only this current owner-hand question needs that projection.
    if (request.kind == AIRequest::RespondCard && request.prompt.startsWith("@taigongyinfu-recast")) {
        for (const Card *card : viewer->getHandcards()) {
            const QString key = "id:" + QString::number(card->getEffectiveId());
            QJsonObject rule = request.worldView.self.cardRules.value(key).toObject();
            QJsonObject limits;
            limits.insert(QString::number(int(Card::MethodRecast)), viewer->isCardLimited(card, Card::MethodRecast, true));
            rule.insert("limits_hand", limits);
            request.worldView.self.cardRules.insert(key, rule);
        }
    }
    if (!yingbian.isEmpty()) context.insert("yingbian", yingbian);
    QJsonObject girls;
    if (request.kind == AIRequest::UseCard && request.pattern == "@@LuaHuowei"
        && viewer->hasSkill("LuaHuowei")) {
        const QVariant value = viewer->getTag("LuaHuoweiDamage");
        if (!value.isValid()) girls.insert("damage", false);
        else if (value.canConvert<DamageStruct>()) {
            const auto damage = value.value<DamageStruct>();
            if (damage.to && damage.to->getRoom() == &m_room)
                girls.insert("damage", QJsonObject{{"from", damage.from ? QJsonValue(damage.from->objectName()) : QJsonValue(false)},
                    {"to", damage.to->objectName()}, {"damage", damage.damage}, {"nature", int(damage.nature)},
                    {"card", damage.card ? QJsonValue(publicDecisionCard(damage.card)) : QJsonValue(false)}});
        }
    }
    if (request.kind == AIRequest::PlayerChosen && request.choiceOptions.reason == "LuaYanling"
        && viewer->hasSkill("LuaYanling")) {
        const QVariant value = viewer->getTag("LuaYanling");
        const Card *card = value.canConvert<const Card *>() ? value.value<const Card *>() : nullptr;
        girls.insert("yanling_card", card ? QJsonValue(publicDecisionCard(card)) : QJsonValue(false));
        if (lua_State *state = m_room.roomRuntime()->lua().state()) {
            const int top = lua_gettop(state);
            lua_pushglobaltable(state); lua_pushliteral(state, "targetlist"); lua_rawget(state, -2);
            bool knownNil = lua_isnil(state, -1);
            if (knownNil && lua_getmetatable(state, -2)) {
                lua_pushliteral(state, "__index"); lua_rawget(state, -2);
                knownNil = lua_isnil(state, -1);
            }
            lua_settop(state, top);
            girls.insert("targetlist_known_nil", knownNil);
        }
    }
    if (!girls.isEmpty()) context.insert("lua_old_enemy_girls", girls);
    if (request.kind == AIRequest::UseCard && request.pattern == "@@dl_liuli" && viewer->hasSkill("dl_liuli")) {
        const QVariant value = m_room.getTag("dl_liuli");
        if (value.canConvert<CardUseStruct>()) {
            const auto use = value.value<CardUseStruct>();
            if (use.card && use.from && use.from->getRoom() == &m_room) {
                QJsonArray names;
                for (const auto *other : m_room.getOtherPlayers(viewer))
                    if (other->getMark("dl_liuli_usefrom") > 0) names.append(other->objectName());
                QJsonObject byCost;
                lua_State *state = m_room.roomRuntime()->lua().state();
                for (const Card *card : viewer->getEquips()) {
                    int rangeFix = card->isKindOf("OffensiveHorse") ? 1 : 0;
                    bool known = true;
                    if (card->isKindOf("Weapon")) {
                        known = false;
                        if (state) {
                            const int top = lua_gettop(state);
                            lua_getglobal(state, "sgs");
                            if (lua_istable(state, -1)) {
                                lua_getfield(state, -1, "weapon_range");
                                if (lua_istable(state, -1)) {
                                    const QByteArray name = card->getClassName().toUtf8();
                                    lua_pushstring(state, name.constData()); lua_rawget(state, -2);
                                    if (lua_isnumber(state, -1)) { rangeFix = int(lua_tonumber(state, -1)) - viewer->getAttackRange(false); known = true; }
                                }
                            }
                            lua_settop(state, top);
                        }
                    }
                    if (!known) continue;
                    QJsonObject targets;
                    for (const auto *target : m_room.getAlivePlayers())
                        targets.insert(target->objectName(), viewer->canSlash(target, nullptr, true, rangeFix));
                    byCost.insert(QString::number(card->getEffectiveId()), targets);
                }
                context.insert("dragonloke_liuli", QJsonObject{{"known", true},
                    {"use", QJsonObject{{"card", publicDecisionCard(use.card)}}},
                    {"source_marked_names", names}, {"can_slash_by_cost", byCost}});
            }
        }
    }
    QJsonObject diva;
    if (viewer->hasSkill("se_nitian") && request.choiceOptions.reason == "se_nitian") {
        if (request.kind == AIRequest::Discard) {
            const auto value = m_room.getTag("se_nitian_judge");
            const JudgeStruct *judge = value.canConvert<JudgeStruct *>() ? value.value<JudgeStruct *>() : nullptr;
            if (judge && judge->who && judge->who->getRoom() == &m_room && judge->card) {
                const auto place = m_room.getCardPlace(judge->card->getEffectiveId());
                if (place == Player::PlaceJudge || place == Player::PlaceTable || place == Player::DiscardPile)
                    diva.insert("nitian_judge", QJsonObject{{"who", judge->who->objectName()},
                        {"card", publicDecisionCard(judge->card)}, {"reason", judge->reason}});
            }
        } else if (request.kind == AIRequest::Choice) {
            const auto value = m_room.getTag("se_nitian_move");
            if (value.canConvert<CardsMoveOneTimeStruct>()) {
                const auto move = value.value<CardsMoveOneTimeStruct>();
                QJsonArray cards;
                bool disclosed = move.to_place == Player::DiscardPile;
                for (int id : move.card_ids) {
                    const Card *card = Sanguosha->getCard(id);
                    if (!card || m_room.getCardPlace(id) != Player::DiscardPile) { disclosed = false; break; }
                    cards.append(publicDecisionCard(card));
                }
                if (disclosed) diva.insert("nitian_move_cards", cards);
            }
        }
    }
    if (request.kind == AIRequest::Choice && request.choiceOptions.reason == "se_zhifucard") {
        QJsonArray armors;
        for (int id = 0; id <= 10000; ++id) {
            const Card *card = Sanguosha->getEngineCard(id);
            if (!card) break;
            if (card->isKindOf("Armor") && request.choiceOptions.choices.contains(card->objectName()))
                armors.append(publicDecisionCard(card));
        }
        diva.insert("armor_catalog", armors);
    }
    if (!diva.isEmpty()) context.insert("dmpdiva", diva);
    request.worldView.strategyFacts.remove("aocai_nojink_scope");
    request.worldView.strategyFacts.remove("response_effect_nojink_token");
    const auto projectNoJinkIdentity = [&](const Card *card) {
        if (!card || !card->isVirtualCard() || !card->getSubcards().isEmpty()) return;
        const QString identity = aiScopedLegacyCardIdentity(card);
        if (!identity.isEmpty()) request.worldView.strategyFacts.insert("response_effect_nojink_token", identity);
    };
    context.insert("effect_present", false);
    context.insert("slash_effect_present", false);
    projectAIStrategyReijyuuChoice(m_room, viewer, request);
    projectAINewstarContext(request, m_room, viewer);
    if (request.kind == AIRequest::SkillInvoke && request.choiceOptions.reason == "ark_jianmo") {
        // extensions/arknights.lua:739/751 uses Room::setPlayerFlag, whose
        // updates are public broadcasts. Preserve the donor's alive-order scan;
        // do not substitute CardUse.from for its possibly different invoker.
        QJsonValue target(false);
        for (const ServerPlayer *candidate : m_room.getAlivePlayers())
            if (candidate->hasFlag("ark_jianmotarget")) { target = candidate->objectName(); break; }
        context.insert("ark_jianmo_target", target);
    }
    // Never retain a previous inquiry after a failed/mismatching re-projection.
    context.remove("lianli_slash_invoke");
    if (request.kind == AIRequest::SkillInvoke && request.choiceOptions.reason == "lianli-slash"
        && data.metaType().id() == QMetaType::QStringList) {
        projectAILianliSlashInvokeDispatch(request, m_room, data.toStringList());
        projectAINestedCardaskCompulsory(request, m_room, viewer);
    }
    if (request.kind == AIRequest::SkillInvoke && request.choiceOptions.reason == "aocai"
        && data.metaType().id() == QMetaType::QStringList) {
        projectAIAocaiInvokeDispatch(request, m_room, data.toStringList());
        projectAINestedCardaskCompulsory(request, m_room, viewer);
        const auto callback = context.value("aocai_invoke").toObject().value("cardask").toObject();
        const auto slash = request.worldView.evaluationCards.value("slash").toObject();
        if (callback.value("known").toBool() && callback.value("present").toBool()
            && callback.value("value_type") == "function" && callback.value("key") == "slash-jink"
            && callback.value("source") == "@lua/ai/standard_cards-ai.lua"
            && callback.value("line").toInt(-1) == 786 && slash.value("effective_id").isDouble())
            request.worldView.strategyFacts.insert("aocai_nojink_scope", QString::number(request.decisionId));
    }
    if (request.kind == AIRequest::Choice && request.choiceOptions.reason == "mouthgun"
        && data.canConvert<PindianStruct *>()) {
        const auto *pindian = data.value<PindianStruct *>();
        const auto publicFace = [&](const Card *card) {
            if (!card) return false;
            const auto place = m_room.getCardPlace(card->getEffectiveId());
            return place == Player::PlaceTable || place == Player::DiscardPile;
        };
        if (pindian && pindian->reason == "mouthgun" && pindian->from && pindian->to
            && (viewer == pindian->from || viewer == pindian->to)
            && publicFace(pindian->from_card) && publicFace(pindian->to_card)) {
            context.insert("pindian", QJsonObject{{"from", pindian->from->objectName()},
                {"to", pindian->to->objectName()}, {"from_card", publicDecisionCard(pindian->from_card)},
                {"to_card", publicDecisionCard(pindian->to_card)}});
            // AnimationCardpack-ai.lua's choice first calls dummyCard("mouthgun").
            // Its registered printed prototype remains cloneable even if its
            // package is absent from patterns()' configured deck catalogue.
            // Match addFunction.lua's NoSuit/0 clone only in this witnessed
            // inquiry. This descriptor is evaluation-only, never a use receipt.
            std::unique_ptr<Card> prototype(Sanguosha->cloneCard("mouthgun", Card::NoSuit, 0));
            if (prototype) {
                QJsonObject row = publicDecisionCard(prototype.get(), true);
                row.insert("evaluation_only", true);
                row.insert("virtual_card", true);
                row.insert("effective_id", -1);
                row.insert("id", -1);
                row.insert("subcards", QJsonArray());
                request.worldView.evaluationCards.insert("mouthgun", row);
            } else {
                request.worldView.evaluationCards.insert("mouthgun", false);
            }
        }
    }
    if (request.kind == AIRequest::Discard && request.choiceOptions.reason == "bunkasai") {
        const Card *card = m_room.getTag("bunkasai").value<const Card *>();
        context.insert("bunkasai_card_present", card != nullptr);
        if (card && card->objectName() == "bunkasai") {
            const CardUseStruct use = m_room.getTag("UseHistory" + card->toString()).value<CardUseStruct>();
            QJsonArray targets;
            for (const ServerPlayer *target : use.to) if (target) targets.append(target->objectName());
            context.insert("bunkasai_use", QJsonObject{{"from", use.from ? use.from->objectName() : QString()},
                {"to", targets}, {"card", publicDecisionCard(use.card)}});
        }
    }
    if (request.kind == AIRequest::Choice && request.choiceOptions.reason == "wengua"
        && data.metaType().id() == QMetaType::QStringList) {
        const QStringList values = data.toStringList();
        bool valid = false; const int id = values.size() == 2 ? values.at(1).toInt(&valid) : -1;
        if (valid && id >= 0 && m_room.findPlayerByObjectName(values.first(), true)
            && m_room.getCardOwner(id) == viewer
            && (m_room.getCardPlace(id) == Player::PlaceHand || m_room.getCardPlace(id) == Player::PlaceEquip))
            context.insert("wengua", QJsonObject{{"donor", values.first()}, {"card_id", id}});
    }
    if (request.kind == AIRequest::Choice && request.choiceOptions.reason == "happycuorui"
        && data.metaType().id() == QMetaType::Int) {
        const int id = data.toInt();
        if (id >= 0 && m_room.getCardPlace(id) == Player::DiscardPile) {
            const Card *card = Sanguosha->getCard(id);
            if (card) context.insert("happycuorui_card", publicDecisionCard(card));
        }
    }
    if (request.kind == AIRequest::SkillInvoke && request.choiceOptions.reason == "LuaOldEnemyHermit") {
        const ServerPlayer *target = m_room.getTag("OldEnemyHermit").value<ServerPlayer *>();
        const QString prompt = data.canConvert<QString>() ? data.toString() : QString();
        const bool known = !target || (target->getRoom() == &m_room
            && prompt == QStringLiteral("Hermit_getCard:") + target->objectName());
        context.insert("old_enemy_hermit", QJsonObject{{"known", known},
            {"target", known && target ? target->objectName() : QString()}});
    }

    if (request.kind == AIRequest::Choice && request.choiceOptions.reason == QStringLiteral("guhuo")
        && data.metaType().id() == QMetaType::QString) {
        const QString value = data.toString();
        const int separator = value.indexOf(':');
        if (separator > 0 && value.indexOf(':', separator + 1) < 0) {
            const QString source = value.left(separator), declaration = value.mid(separator + 1);
            if (m_room.findPlayerByObjectName(source, true) && Sanguosha->getCardNames().contains(declaration))
                context.insert("guhuo", QJsonObject{{"source", source}, {"declaration", declaration}});
        }
    }
    if (data.canConvert<JudgeStruct *>()) {
        const JudgeStruct *judge = data.value<JudgeStruct *>();
        if (!judge || !judge->who) return;
        context.insert("judge", projectAIStrategyJudgeContext(request, m_room, viewer, judge,
            [](const Card *card) { return publicDecisionCard(card); }));
        const bool publicPattern = context.value("judge").toObject().value("unfiltered_outcomes_complete").toBool();
        if (request.kind == AIRequest::SkillInvoke && request.choiceOptions.reason == "yl_dayuan"
            && judge->card && publicPattern) {
            QJsonObject hypothetical;
            for (int suit = 3; suit >= 0; --suit) {
                for (int number = 13; number >= 1; --number) {
                    std::unique_ptr<Card> candidate(Sanguosha->cloneCard(judge->card->objectName(), Card::Suit(suit), number));
                    if (!candidate) { hypothetical = QJsonObject(); break; }
                    hypothetical.insert(QString::number(suit) + ':' + QString::number(number), judge->isGood(candidate.get()));
                }
            }
            if (hypothetical.size() == 52) {
                auto projected = context.value("judge").toObject();
                projected.insert("candidate_outcomes", hypothetical); context.insert("judge", projected);
            }
        }
    } else if (data.canConvert<DamageStruct>()) {
        const DamageStruct damage = data.value<DamageStruct>();
        context.insert("damage", QJsonObject{
            {"from", damage.from ? damage.from->objectName() : QString()},
            {"to", damage.to ? damage.to->objectName() : QString()},
            {"amount", damage.damage}, {"nature", int(damage.nature)}, {"reason", damage.reason},
            {"card", publicDecisionCard(damage.card, damage.from == viewer)},
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
                {"reason", damage.reason}, {"card", publicDecisionCard(damage.card, damage.from == viewer)},
                {"card_name", damage.card ? damage.card->objectName() : QString()}});
        }
        context.insert(QStringLiteral("dying"), dyingObject);
    } else if (data.canConvert<CardResponseStruct>()) {
        const CardResponseStruct response = data.value<CardResponseStruct>();
        context.insert("card", publicDecisionCard(response.m_card));
    } else if (data.canConvert<CardEffectStruct>()) {
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        request.worldView.strategyFacts.insert("response_effect_card", publicDecisionCard(effect.card, effect.from == viewer));
        projectNoJinkIdentity(effect.card);
        if (request.kind == AIRequest::Choice && request.choiceOptions.reason == "heg_nullification" && effect.card) {
            const QString tagName = "targets" + effect.card->toString();
            QJsonArray targets;
            for (const QVariant &value : m_room.getTag(tagName).toList()) {
                const auto *target = value.value<ServerPlayer *>();
                if (target && m_room.getAllPlayers(true).contains(const_cast<ServerPlayer *>(target)))
                    targets.append(target->objectName());
            }
            context.insert("heg_nullification", QJsonObject{
                {"targets_present", m_room.getAllTags().contains(tagName)}, {"targets", targets}});
        }
        context.insert("effect_present", true);
        context.insert("effect", QJsonObject{
            {"from", effect.from ? effect.from->objectName() : QString()},
            {"to", effect.to ? effect.to->objectName() : QString()},
            {"card", publicDecisionCard(effect.card, effect.from == viewer)},
            {"card_name", effect.card ? effect.card->objectName() : QString()}, {"offset_num", effect.offset_num}});
    } else if (data.canConvert<SlashEffectStruct>()) {
        const SlashEffectStruct effect = data.value<SlashEffectStruct>();
        context.insert("slash_effect_present", true);
        context.insert("slash_effect", QJsonObject{
            {"from", effect.from ? effect.from->objectName() : QString()},
            {"to", effect.to ? effect.to->objectName() : QString()},
            {"card", publicDecisionCard(effect.slash, effect.from == viewer)},
            {"card_present", effect.slash != nullptr}, {"nature", int(effect.nature)},
            {"jink_num", effect.jink_num}, {"drank", effect.drank},
            {"multiple", effect.multiple}, {"nullified", effect.nullified},
            {"no_respond", effect.no_respond}, {"no_offset", effect.no_offset}});
        request.worldView.strategyFacts.insert("response_effect_card", publicDecisionCard(effect.slash, effect.from == viewer));
        projectNoJinkIdentity(effect.slash);
        context.insert("effect_present", true);
        context.insert("effect", QJsonObject{{"from", effect.from ? effect.from->objectName() : QString()},
            {"to", effect.to ? effect.to->objectName() : QString()}, {"card", publicDecisionCard(effect.slash, effect.from == viewer)},
            {"card_name", effect.slash ? effect.slash->objectName() : QString()}, {"offset_num", effect.jink_num}});
        if (effect.to == viewer) context.insert("expected_jink_num", effect.jink_num);
    } else if (data.canConvert<CardsMoveOneTimeStruct>()) {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        QJsonObject projected{{"from", move.from ? move.from->objectName() : QString()},
            {"to", move.to ? move.to->objectName() : QString()}, {"to_place", int(move.to_place)},
            {"card_count", move.card_ids.size()},
            {"reason", QJsonObject{{"reason", move.reason.m_reason}, {"skill_name", move.reason.m_skillName}}}};
        // The involved owner and moves to public zones already observe these
        // identities. Other hidden hand transfers remain count-only.
        if (move.from == viewer || move.to == viewer || move.to_place == Player::DiscardPile
            || move.to_place == Player::PlaceEquip || move.to_place == Player::PlaceDelayedTrick)
            projected.insert("card_ids", responseIds(move.card_ids));
        context.insert("move", projected);
    } else if (data.canConvert<CardUseStruct>()) {
        const CardUseStruct use = data.value<CardUseStruct>();
        QJsonArray targets;
        for (const ServerPlayer *target : use.to)
            if (target) targets.append(target->objectName());
        if (request.kind == AIRequest::SkillInvoke && request.choiceOptions.reason == QStringLiteral("fan")
            && use.from == viewer)
            projectAIStrategyFanProbe(request.worldView, m_room, viewer, use.card);
        // The question exposes the declared use, never its hidden payment IDs.
        context.insert("use", QJsonObject{
            {"from", use.from ? use.from->objectName() : QString()},
            {"to", targets}, {"m_addHistory", use.m_addHistory}, {"card", publicDecisionCard(use.card, use.from == viewer)}});
        if(request.kind==AIRequest::SkillInvoke&&request.choiceOptions.reason=="lianzhan"&&use.from==viewer&&use.card){
            auto row=context.value("use").toObject();auto card=row.value("card").toObject();
            card.insert("legacy_identity_token",aiLegacyCardIdentity(m_room,viewer,use.card));row.insert("card",card);context.insert("use",row);
        }
    } else if (data.canConvert<ServerPlayer *>()) {
        const ServerPlayer *target = data.value<ServerPlayer *>();
        // Only reference the player in the viewer's existing world snapshot.
        if (target) context.insert("player", target->objectName());
    }
    // Named question tags are read only in their native ask windows.
    const auto playerName = [&](const QVariant &value) -> QJsonValue {
        const auto *target = value.canConvert<ServerPlayer *>() ? value.value<ServerPlayer *>() : nullptr;
        return target && target->getRoom() == &m_room ? QJsonValue(target->objectName()) : QJsonValue(false);
    };
    const auto useValue = [&](const QVariant &value) -> QJsonValue {
        if (!value.canConvert<CardUseStruct>()) return false;
        const auto use = value.value<CardUseStruct>();
        QJsonArray names;
        for (const auto *target : use.to) if (target && target->getRoom() == &m_room) names.append(target->objectName());
        return QJsonObject{{"from", use.from ? use.from->objectName() : QString()}, {"to", names},
            {"card", publicDecisionCard(use.card, use.from == viewer)}, {"m_addHistory", use.m_addHistory}};
    };
    if (request.kind == AIRequest::Suit && (question == "mouhongyan" || question == "SixGuicai")) {
        const QVariant value = m_room.getTag(question == "mouhongyan" ? "mouhongyan" : "CurrentJudgeStruct");
        const auto *judge = value.canConvert<JudgeStruct *>() ? value.value<JudgeStruct *>() : nullptr;
        QJsonValue projected = false;
        if (judge && judge->who && judge->card) {
            QJsonObject row{{"scope_key", question}, {"who", judge->who->objectName()},
                {"reason", judge->reason}, {"pattern", judge->pattern}, {"predicate_good", judge->good},
                {"negative", judge->negative}, {"good", judge->isGood()}, {"card", publicDecisionCard(judge->card)}};
            bool publicPattern = !judge->pattern.contains('$');
            for (const QString &branch : judge->pattern.split('#')) {
                const auto factors = branch.split('|');
                if (factors.size() >= 4 && factors.at(3) != ".") publicPattern = false;
            }
            if (publicPattern) {
                QJsonObject candidates, outcomes;
                for (int suit : {3, 2, 0, 1}) {
                    std::unique_ptr<Card> candidate(Sanguosha->cloneCard(judge->card->objectName(), Card::Suit(suit), 0));
                    if (!candidate) continue;
                    const QString key = QString::number(suit) + ":0";
                    auto face = publicDecisionCard(candidate.get());
                    face.insert("evaluation_only", true); face.insert("virtual_card", true);
                    face.insert("named_judge_scope", question); face.insert("named_judge_key", key);
                    candidates.insert(key, face); outcomes.insert(key, judge->isGood(candidate.get()));
                }
                if (candidates.size() == 4) { row.insert("readonly_candidates", candidates); row.insert("candidate_outcomes", outcomes); }
            }
            projected = row;
        }
        if (question == "mouhongyan") context.insert("newgenerals", QJsonObject{{"mouhongyan", projected}});
        else context.insert("biaofeng_judge", projected);
    }
    if (request.kind == AIRequest::RespondCard && request.choiceOptions.question == "askForCard") {
        const QString promptKey = request.prompt.section(QLatin1Char(':'), 0, 0);
        if (promptKey == "@skeleton" || promptKey == "@tobenatureAsk") {
            // The source sorts its whole owner HE set before matching. Offered
            // response admission is a separate filter and cannot replace this.
            QJsonObject matches;
            for (const Card *card : viewer->getCards("he"))
                matches.insert(QString::number(card->getEffectiveId()),
                    Sanguosha->matchExpPattern(request.pattern, viewer, card));
            context.insert("yuri_pattern_matches", QJsonObject{{"pattern", request.pattern}, {"by_id", matches}});
        }
        if (promptKey == "@arkoraask") request.worldView.strategyFacts.insert("yuri_arkora_delay", true);
    }
    if (request.kind == AIRequest::UseCard
        && (request.pattern == "@@meizlchunshen" || request.pattern == "@@meizlshchunshen")) {
        const QString key = request.pattern == "@@meizlchunshen" ? "meizlchunshenDamage" : "meizlshchunshenDamage";
        const QVariant value = m_room.getTag(key);
        QJsonValue projected = false;
        if (value.canConvert<DamageStruct>()) {
            const auto damage = value.value<DamageStruct>();
            projected = QJsonObject{{"from", damage.from ? QJsonValue(damage.from->objectName()) : QJsonValue(false)},
                {"to", damage.to ? QJsonValue(damage.to->objectName()) : QJsonValue(false)},
                {"card", damage.card ? QJsonValue(publicDecisionCard(damage.card)) : QJsonValue(false)},
                {"damage", damage.damage}, {"nature", int(damage.nature)}, {"reason", damage.reason},
                {"chain", damage.chain}, {"transfer", damage.transfer}, {"by_user", damage.by_user}};
        }
        context.insert("meizl", QJsonObject{{key, projected}});
    }
    if (question == "f_qigong" && request.kind == AIRequest::PlayerChosen) {
        // Both fixed genius.lua writers fillAG publicly before this exact ask,
        // then clear the tag immediately after. The policy sees no other pile.
        const Card *card = m_room.getTag("f_qigong").value<const Card *>();
        if (card && card->isKindOf("EquipCard"))
            context.insert("genius_qigong_card", publicDecisionCard(card));
        else if (!card) context.insert("genius_qigong_card", false);
    }
    if (question == "sy_luanzheng" && request.kind == AIRequest::PlayerChosen)
        context.insert("sy", QJsonObject{{"luanzheng_use", useValue(viewer->getTag("luanzheng_data"))}});
    if (question == "fateloli_vs" && request.kind == AIRequest::AmazingGrace)
        context.insert("fate_loli_target", playerName(m_room.getTag("fateloli")));
    if (request.kind == AIRequest::UseCard && request.pattern == "@@LuaQisi") {
        const QVariant value = m_room.getTag("LuaQisiData");
        if (value.canConvert<CardEffectStruct>()) {
            const auto effect = value.value<CardEffectStruct>();
            context.insert("erciyuan_qisi_effect", QJsonObject{
                {"from", effect.from ? effect.from->objectName() : QString()},
                {"to", effect.to ? effect.to->objectName() : QString()},
                {"card", publicDecisionCard(effect.card)}, {"offset_num", effect.offset_num},
                {"multiple", effect.multiple}, {"nullified", effect.nullified},
                {"no_respond", effect.no_respond}, {"no_offset", effect.no_offset},
                {"extra_effect", effect.extra_effect}});
        }
    }
    if (request.kind == AIRequest::AmazingGrace && question == "slyanhuo") {
        const QVariant value = m_room.getTag("slyanhuoData");
        if (value.canConvert<DamageStruct>()) {
            const auto damage = value.value<DamageStruct>();
            context.insert("erciyuan_slyanhuo_damage", QJsonObject{
                {"from", damage.from ? damage.from->objectName() : QString()},
                {"to", damage.to ? damage.to->objectName() : QString()},
                {"amount", damage.damage}, {"nature", int(damage.nature)},
                {"reason", damage.reason}, {"chain", damage.chain},
                {"transfer", damage.transfer}, {"by_user", damage.by_user},
                {"card", publicDecisionCard(damage.card)}});
        }
    }
    if (question == "fatehuyou" && request.kind == AIRequest::PlayerChosen) {
        const QVariant value = m_room.getTag("fatehuyou");
        if (value.canConvert<CardEffectStruct>()) {
            const auto effect = value.value<CardEffectStruct>();
            context.insert("fate_huyou_effect", QJsonObject{
                {"from", effect.from ? effect.from->objectName() : QString()},
                {"to", effect.to ? effect.to->objectName() : QString()},
                {"card", publicDecisionCard(effect.card)}, {"offset_num", effect.offset_num}});
        }
    }
    if (request.kind == AIRequest::UseCard && request.pattern == "@@ckshiliang") {
        const auto use = useValue(m_room.getTag("ckshiliang"));
        if (use.isObject()) context.insert("use", use);
    }
    if (request.kind == AIRequest::SkillInvoke && question == "ckyongchang") {
        const auto value = m_room.getTag("ckyongchang");
        if (value.canConvert<CardEffectStruct>()) {
            const auto effect = value.value<CardEffectStruct>();
            context.insert("effect_present", true);
            context.insert("effect", QJsonObject{{"from", effect.from ? effect.from->objectName() : QString()},
                {"to", effect.to ? effect.to->objectName() : QString()}, {"card", publicDecisionCard(effect.card)},
                {"card_name", effect.card ? effect.card->objectName() : QString()}, {"offset_num", effect.offset_num}});
        }
    }
    if (question == "sy_quanqing" && (request.kind == AIRequest::AmazingGrace || request.kind == AIRequest::PlayerChosen))
        context.insert("mojiang_quanqing_target", playerName(viewer->getTag("sy_quanqing_target")));
    if (question == "sy_diaoling" && request.kind == AIRequest::SkillInvoke) {
        const auto value = viewer->getTag("sy_diaoling_move");
        QJsonObject row{{"target", playerName(viewer->getTag("sy_diaoling_target"))}};
        if (value.canConvert<CardsMoveOneTimeStruct>()) row.insert("count", value.value<CardsMoveOneTimeStruct>().card_ids.size());
        context.insert("mojiang_diaoling", row);
    }
    if (question == "yx_sword" && request.kind == AIRequest::PlayerChosen) {
        const auto value = m_room.getTag("YxSwordData");
        if (value.canConvert<DamageStruct>()) {
            const auto damage = value.value<DamageStruct>();
            context.insert("joyequip", QJsonObject{{"yx_sword_damage", QJsonObject{
                {"from", damage.from ? damage.from->objectName() : QString()}, {"to", damage.to ? damage.to->objectName() : QString()},
                {"damage", damage.damage}, {"nature", int(damage.nature)}, {"chain", damage.chain},
                {"transfer", damage.transfer}, {"by_user", damage.by_user}, {"card", publicDecisionCard(damage.card)}}}});
        }
    }
    if (question == "huanshi" && request.kind == AIRequest::AmazingGrace) {
        const auto value = viewer->getTag("HuanshiJudge");
        const auto *judge = value.canConvert<JudgeStruct *>() ? value.value<JudgeStruct *>() : nullptr;
        if (judge && judge->who && judge->card)
            context.insert("judge", QJsonObject{{"who", judge->who->objectName()}, {"reason", judge->reason},
                {"pattern", judge->pattern}, {"good", judge->isGood()}, {"predicate_good", judge->good},
                {"negative", judge->negative}, {"card", publicDecisionCard(judge->card)}});
    }
    if (request.kind == AIRequest::UseCard && request.pattern == "@@fuzuo") {
        const auto value = m_room.getTag("FuzuoPindianData");
        const auto *pindian = value.canConvert<PindianStruct *>() ? value.value<PindianStruct *>() : nullptr;
        if (pindian && pindian->from && pindian->to)
            context.insert("wisdom", QJsonObject{{"fuzuo_pindian", QJsonObject{
                {"from", pindian->from->objectName()}, {"to", pindian->to->objectName()},
                {"from_number", pindian->from_number}, {"to_number", pindian->to_number}, {"reason", pindian->reason},
                {"from_card", publicDecisionCard(pindian->from_card)}, {"to_card", publicDecisionCard(pindian->to_card)}}}});
    }
    if ((request.kind == AIRequest::UseCard && request.pattern.contains("EXCard_WWJZ"))
        || question.startsWith("EXCard_") || (request.kind == AIRequest::RespondCard && request.pattern.contains("nullification"))) {
        QJsonArray dhls, fldf;
        for (const auto *target : m_room.getAllPlayers(true)) {
            if (target->hasFlag("EXCard_DHLS_target2")) dhls.append(target->objectName());
            if (target->hasFlag("EXCard_FLDF_Skill")) fldf.append(target->objectName());
        }
        context.insert("excard2014", QJsonObject{{"wwjz_use", useValue(m_room.getTag("EXCard_WWJZ_data"))},
            {"dhls_targets", dhls}, {"fldf_flagged_names", fldf}});
    }
    if (request.kind == AIRequest::RespondCard && request.prompt.startsWith("@Guard")) {
        QJsonObject horses;
        QSet<int> disclosed;
        for (const auto &card : request.worldView.handCards) disclosed.insert(card.effectiveId);
        const auto addDisclosed = [&](const AIPlayerView &view) {
            for (const auto &card : view.knownCards) disclosed.insert(card.effectiveId);
            for (const auto &card : view.equips) disclosed.insert(card.effectiveId);
            for (const auto &pile : view.piles) for (const auto &card : pile.cards) disclosed.insert(card.effectiveId);
        };
        addDisclosed(request.worldView.self); for (const auto &view : request.worldView.players) addDisclosed(view);
        for (int id : disclosed) {
            const auto *card = Sanguosha->getCard(id);
            if (card && card->isKindOf("Horse")) horses.insert(QString::number(id), publicDecisionCard(card->getRealCard()));
        }
        QJsonObject row{{"real_horses", horses}};
        QJsonArray cards; bool complete = true;
        const QString value = viewer->property("ronghe").toString();
        if (!value.isEmpty()) for (const QString &part : value.split('+')) {
            bool valid = false; const int id = part.toInt(&valid);
            const auto *card = valid && disclosed.contains(id) ? Sanguosha->getCard(id) : nullptr;
            if (!card) { complete = false; break; }
            cards.append(publicDecisionCard(card));
        }
        if (complete) row.insert("ronghe_cards", cards);
        context.insert("gaodacard", row);
    }
    if (request.kind == AIRequest::Choice && question == "luaRcaiyi") {
        // Native askForChoice supplies no card identities to the donor data argument.
        QJsonArray ids;
        for (const auto &value : data.toList()) if (value.canConvert<int>()) ids.append(value.toInt());
        context.insert("shengrongsiai_caiyi_ids", ids);
    }
    QJsonObject yongjian{{"poison_use", viewer->property("PoisonUse").toString()},
        {"tuixinzhifu_target", playerName(viewer->getTag("yj_tuixinzhifu"))}};
    const auto caochuan = viewer->getTag("Caochuanjiejian");
    if (!caochuan.isValid()) yongjian.insert("caochuan_effect", false);
    else if (caochuan.canConvert<CardEffectStruct>()) {
        const auto effect = caochuan.value<CardEffectStruct>();
        yongjian.insert("caochuan_effect", QJsonObject{{"from", effect.from ? effect.from->objectName() : QString()},
            {"to", effect.to ? effect.to->objectName() : QString()}, {"card", publicDecisionCard(effect.card)}, {"offset_num", effect.offset_num}, {"nullified", effect.nullified}});
    }
    const auto jinchan = viewer->getTag("Jinchan");
    if (jinchan.canConvert<CardUseStruct>()) {
        auto row = useValue(jinchan).toObject();
        row.insert("nullified_list", QJsonArray::fromStringList(jinchan.value<CardUseStruct>().nullified_list));
        yongjian.insert("jinchan_use", row);
    } else if (!jinchan.isValid()) yongjian.insert("jinchan_use", false);
    // The bare tag is the donor key. Never substitute its newer per-card tag.
    QJsonArray zhulutianxia; bool zhulutianxiaKnown = true;
    for (const auto &value : m_room.getTag("Zhulutianxia").toList()) {
        const int id = value.toInt(); const auto place = m_room.getCardPlace(id);
        if (place != Player::PlaceEquip && place != Player::PlaceDelayedTrick
            && place != Player::PlaceTable && place != Player::DiscardPile
            && m_room.getCardOwner(id) != viewer) { zhulutianxiaKnown = false; break; }
        zhulutianxia.append(id);
    }
    if (zhulutianxiaKnown) yongjian.insert("zhulutianxia_ids", zhulutianxia);
    context.insert("yongjian", yongjian);
    if (request.kind == AIRequest::SkillInvoke && request.choiceOptions.reason == "jieneng"
        && data.canConvert<CardUseStruct>()) {
        const auto use = data.value<CardUseStruct>();
        const int index = use.to.indexOf(viewer);
        if (use.from && use.card && use.card->isKindOf("Slash") && index >= 0) {
            // gaoda-ai.lua5774 reads the first index, not a maximum across the
            // target vector. Project precisely that viewer's pending scalar.
            const auto numbers = use.from->getTag("Jink_" + use.card->toString()).toList();
            if (index < numbers.size()) {
                bool ok = false; const int expected = numbers.at(index).toInt(&ok);
                if (ok) {
                    auto projected = context.value("use").toObject();
                    projected.insert("expected_jink_num", expected);
                    context.insert("use", projected);
                }
            }
        }
    }
    // Only the current viewer's own pending Slash requirement is disclosed,
    // never another player's whole Jink_ vector or an arbitrary tag query.
    if (data.canConvert<CardUseStruct>() && request.choiceOptions.question == "askForCard"
        && request.prompt.section(':', 0, 0) == "double-sword-card") {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from && use.card && use.card->isKindOf("Slash") && use.to.contains(viewer)) {
            const QStringList numbers = use.from->getTag("Jink_" + use.card->toString()).toStringList();
            bool known = true; int expected = 1;
            for (int index = 0; index < use.to.size(); ++index) {
                if (use.to[index] != viewer) continue;
                bool ok = false;
                const int number = numbers.value(index).toInt(&ok);
                if (!ok) { known = false; break; }
                if (number == 0) { expected = 0; break; }
                expected = qMax(expected, number);
            }
            if (known) {
                QJsonObject projected = context.value("use").toObject();
                projected.insert("expected_jink_num", expected);
                context.insert("use", projected);
            }
        }
    }
    if (request.choiceOptions.reason == QStringLiteral("sgkgodlinglong")) {
        if (data.canConvert<MaxHpStruct>())
            context.insert("maxhp", QJsonObject{{"change", data.value<MaxHpStruct>().change}});
        if (data.canConvert<HpLostStruct>())
            context.insert("hp_lost", QJsonObject{{"lose", data.value<HpLostStruct>().lose}});
    }
    // Some asks pass only a player, and keep the surrounding struct in a room tag
    // for the duration of that question. Project those tags as the same value
    // objects; do not invent a struct the tag does not currently hold.
    if (!context.contains(QStringLiteral("damage"))) {
        const QVariant tagged = m_room.getTag(QStringLiteral("CurrentDamageStruct"));
        if (tagged.canConvert<DamageStruct>()) {
            const DamageStruct damage = tagged.value<DamageStruct>();
            context.insert(QStringLiteral("damage"), QJsonObject{
                {"from", damage.from ? damage.from->objectName() : QString()},
                {"to", damage.to ? damage.to->objectName() : QString()},
                {"amount", damage.damage}, {"nature", int(damage.nature)},
                {"reason", damage.reason}, {"card", publicDecisionCard(damage.card, damage.from == viewer)},
                {"card_name", damage.card ? damage.card->objectName() : QString()}});
        }
    }
    if (!context.contains(QStringLiteral("use"))) {
        const QVariant tagged = m_room.getTag(QStringLiteral("CurrentUseStruct"));
        if (tagged.canConvert<CardUseStruct>()) {
            const CardUseStruct use = tagged.value<CardUseStruct>();
            if (request.kind == AIRequest::SkillInvoke && request.choiceOptions.reason == QStringLiteral("fan")
                && use.from == viewer)
                projectAIStrategyFanProbe(request.worldView, m_room, viewer, use.card);
            QJsonArray targets;
            for (const ServerPlayer *target : use.to)
                if (target) targets.append(target->objectName());
            context.insert(QStringLiteral("use"), QJsonObject{
                {"from", use.from ? use.from->objectName() : QString()},
                {"to", targets}, {"m_addHistory", use.m_addHistory}, {"card", publicDecisionCard(use.card, use.from == viewer)}});
        }
    }
    projectAIStrategyScarletContext(request, m_room, viewer, data, context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIStrategyResponseContext(request, m_room, viewer, data, context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIStrategyRound24Response(request, m_room, viewer, data, context);
    projectAIStrategyRound31Response(request, m_room, viewer, data, context);
    projectAIRound50HTMSEvents(request, m_room, viewer,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound43GoddessContext(request, m_room, viewer, data, context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIStrategyRetrialContext(request, m_room, viewer, data, context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIStrategyChoiceContext(request, m_room, viewer, data, context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIStrategyRound21OriginalRequest(request, m_room, viewer, data, context);
    projectAIRound21PlanningContext(request, m_room, viewer,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound24FeiDIYContext(request, m_room, viewer,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound24ChoiceContext(request, m_room, viewer);
    projectAIRound25ChoiceContext(request, m_room, viewer, data, context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound26ChoiceContext(request, m_room, viewer, data, context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound27ChoiceContext(request, m_room, viewer, data, context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIStrategyRound25Response(request, m_room, viewer, context);
    projectAIRound25OriginalContext(request, m_room, viewer, data,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound26OriginalContext(request, m_room, viewer, data,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound25PlanningContext(request, m_room, viewer, data,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound26PlanningContext(request, m_room, viewer, data, context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound28PlanningContext(request, m_room, viewer, data, context,
        [](const Card *card) { return publicDecisionCard(card); },
        [](const Card *card, int, const QString &) { return publicDecisionCard(card); });
    projectAIRound27PlanningContext(request, m_room, viewer, data, context,
        [](const Card *card) { return publicDecisionCard(card); },
        [&](const Card *card) {
            // Exact current Huanshi judge face, evaluated from the prompted
            // owner's perspective for the donor's exchange/use-value branch.
            // This readonly profile never makes the judge card a payable card.
            // Virtual material provenance is outside this physical judge scope.
            if (card->isVirtualCard()) return publicDecisionCard(card);
            auto row = publicDecisionCard(card);
            row.insert("evaluation_only", true); row.insert("evaluation_owner", viewer->objectName());
            row.insert("evaluation_rules", responseOwnerRules(m_room, viewer, card));
            row.insert("target_residue", responseResidueJson(responseResidue(m_room, viewer, card)));
            row.insert("target_fixed", card->targetFixed()); row.insert("can_recast", card->canRecast());
            row.insert("handling_method", int(card->getHandlingMethod())); row.insert("virtual_card", card->isVirtualCard());
            row.insert("subcards", responseIds(card->getSubcards()));
            int budget = 4096;
            row.insert("evaluation_candidate", responseCandidateJson(makeAICardCandidate(
                m_room, viewer, card, Card::MethodUse, AIRequest::Activate, QString(), budget)));
            return row;
        });
    projectAIRound21ChoiceContext(request, m_room, viewer, context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound22OlClanPlanContext(request, m_room, viewer);
    projectAIRound22ChoiceContext(request, m_room, viewer, context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound23ChoiceContext(request, m_room, viewer, data, context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIStrategyRound22OlClanObservations(request, m_room, viewer, data, context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound24OriginalContext(request, m_room, viewer, data,
        [](const Card *card) { return publicDecisionCard(card); });
// Insert near END of projectDecisionContext, after all existing Yingbian and
// JiePackage tag producers (otherwise an older native value can overwrite it).
// The returned source is always this viewer's observed CardUse, never foreign
// canUse or private response vectors.
const AIRound26UseProfile delegateProfile = [&](const Card *card) {
    auto row = publicDecisionCard(card);
    row.insert("evaluation_only", true);
    row.insert("evaluation_owner", viewer->objectName());
    row.insert("evaluation_rules", responseOwnerRules(m_room, viewer, card));
    row.insert("target_residue", responseResidueJson(responseResidue(m_room, viewer, card)));
    int budget = 4096; // existing exact Yingbian profile bound, not an increased query budget
    row.insert("evaluation_candidate", responseCandidateJson(makeAICardCandidate(
        m_room, viewer, card, Card::MethodUse, AIRequest::Activate, QString(), budget)));
    QJsonObject values;
    const auto facts = projectAICanUseTargets(m_room, viewer, card);
    for (auto it=facts.cbegin(); it!=facts.cend(); ++it) values.insert(it.key(),it.value());
    row.insert("can_use_by_player",QJsonObject{{viewer->objectName(),values}});
    return row;
};
const AIRound26UseProfile constructedLihuoProfile = [&](const Card *card) {
    auto row=delegateProfile(card);const auto face=lihuoPolicyCard(card);
    for(auto it=face.begin();it!=face.end();++it)row.insert(it.key(),it.value());
    row.insert("locked",viewer->isLocked(card));row.insert("using",card->hasFlag("using"));
    return row;
};
projectAIRound28LihuoQuestion(request,m_room,viewer,data);
projectAIRound45InovationContext(request,m_room,viewer,data);
projectAIRound26Delegates(request,m_room,viewer,delegateProfile,data);
for(const QString &destination:{QString("JincanmouData"),QString("yb_zhuzhan2_data"),QString("yb_fujia2_data")}) {
    const auto saved=restoreAIRound26DelegateUse(request,viewer,destination,delegateProfile,constructedLihuoProfile);
    if(!saved.value("present").toBool())continue; // native current-tag producer wins
    const bool known=saved.value("known")==true;
    if(destination=="JincanmouData") {
        auto outer=context.value("jie_package").toObject();
        if(known)outer.insert(destination,saved.value("use"));
        else outer.remove(destination); // strict unknown; NEVER source-known absent false
        context.insert("jie_package",outer);
    } else {
        auto outer=context.value("yingbian").toObject();
        if(known)outer.insert("use",saved.value("use"));
        else outer.remove("use");
        context.insert("yingbian",outer);
    }
}
    // This finite question producer is independent of saved delegate uses.
    // In particular, !saved.present above must not skip current owner context.
    projectAIRound45Newgenerals(request,m_room,viewer,[](const Card *card){return publicDecisionCard(card);});
    projectAIRound50YuriQuestion(request,m_room,viewer,[](const Card*card){return publicDecisionCard(card);});

    // Earlier context producers rebuild the Yongjian object; retain the finite
    // history witness only after the final projection handoff.
    projectAIRound40TargetHistory(request, m_room, viewer);
}

bool AiDecisionCoordinator::decideSkillInvoke(ServerPlayer *player, const QString &skillName,
                                              const QVariant &data, bool &invoked) const
{
    if (!player || !player->getAI()) return false;
    AIChoiceOptions options;
    options.question = QStringLiteral("askForSkillInvoke");
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
    return commitAcceptedChoiceEffects(m_room, request, result);
}

bool AiDecisionCoordinator::decideChoice(ServerPlayer *player, const QString &skillName,
                                         const QString &choices, const QVariant &data,
                                         QString &answer) const
{
    if (!player || !player->getAI()) return false;
    AIChoiceOptions options;
    options.question = QStringLiteral("askForChoice");
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
    if (result.kind != AIResult::Answer) return false;
    if (result.action.userString.isEmpty()
        && !(result.originalEmptyChoice && request.kind==AIRequest::Choice && request.choiceOptions.question=="askForChoice"
            && aiRound35OriginalQuestion(request)))
        return false; // Ordinary refusal retains the existing call-site default.
    answer = result.action.userString;
    return commitAcceptedChoiceEffects(m_room, request, result);
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
    AIRequest request = makeChoiceRequest(player, AIRequest::Suit, options);
    if (!runAnswer(player, request,
                   QStringLiteral("askForSuit"),
                   [ai, &reason](const AIRequest &request) {
                       return legacyAnswerResult(request, Card::Suit2String(ai->askForSuit(reason)));
                   }, result))
        return false;
    if (result.kind != AIResult::Answer) return false;
    if (result.originalSuitResult) {
        if (!aiRound35OriginalQuestion(request) || result.originalSuitValue<int(Card::SuitToBeDecided)
            || result.originalSuitValue>int(Card::NoSuit)) return false;
        suit=Card::Suit(result.originalSuitValue);
        return commitAcceptedChoiceEffects(m_room,request,result);
    }
    // Suits cross as their names; an unknown name is refused, never guessed.
    for (int index = 0; index < 4; ++index) {
        if (Card::Suit2String(Card::AllSuits[index]) == result.action.userString) {
            suit = Card::AllSuits[index];
            return commitAcceptedChoiceEffects(m_room, request, result);
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
    AIRequest request = makeChoiceRequest(player, AIRequest::Kingdom, options);
    if (!runAnswer(player, request,
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
    return commitAcceptedChoiceEffects(m_room, request, result);
}

bool AiDecisionCoordinator::decideGeneral(ServerPlayer *player, const QStringList &generals,
                                          const QString &defaultChoice, const QString &reason,
                                          QString &answer) const
{
    if (!player || !player->getAI()) return false;
    AIChoiceOptions options;
    options.reason = reason;
    options.choices = generals;
    QJsonObject catalog;
    for (const QString &name : generals) {
        const General *general = Sanguosha->getGeneral(name);
        QJsonObject row{{"exists", general != nullptr}};
        if (general) {
            row.insert("kingdom", general->getKingdom());
            row.insert("skill_description", general->getSkillDescription(true));
        }
        catalog.insert(name, row);
    }
    options.context.insert("general_catalog", catalog);
    projectAIRound40OLGeneralContext(m_room,player,generals,options.context);
    ServerPlayer *lord = m_room.getLord();
    if (lord && lord->getGeneral())
        options.context.insert("lord_general_kingdom", lord->getGeneral()->getKingdom());
    options.defaultChoice = defaultChoice;
    options.hasDefaultChoice = !defaultChoice.isEmpty();
    bool selectable = false;
    for (const QString &name : generals) if (!name.startsWith('~')) { selectable = true; break; }
    QString randomToken;
    if (!selectable && !defaultChoice.isEmpty())
        options.context.insert("empty_general_fallback", defaultChoice);
    else if (!selectable) {
        randomToken = "isolated-default-general:" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        options.context.insert("empty_general_random_token", randomToken);
    }
    AI *ai = player->getAI();
    AIResult result;
    AIRequest request = makeChoiceRequest(player, AIRequest::General, options);
    if (!runAnswer(player, request,
                   QStringLiteral("askForGeneral"),
                   [ai, &generals, &defaultChoice, &reason](const AIRequest &request) {
                       return legacyAnswerResult(request,
                           ai->askForGeneral(generals, defaultChoice, reason));
                   }, result))
        return false;
    if (result.kind != AIResult::Answer || result.action.userString.isEmpty())
        return false;
    if (!randomToken.isEmpty() && result.action.userString == randomToken) {
        // The empty SmartAI result enters TrustAI's native RNG branch. The
        // isolated policy requests precisely this branch without drawing its
        // separate AI RNG or learning a new hidden value.
        if (selectable || !defaultChoice.isEmpty()) return false;
        answer = generals.isEmpty() ? QStringLiteral("caocao")
            : generals.at(m_room.roomRuntime()->rng().bounded(generals.size()));
    } else answer = result.action.userString;
    return commitAcceptedChoiceEffects(m_room, request, result);
}

bool AiDecisionCoordinator::decideDiscard(ServerPlayer *player, const QString &reason,
                                          int discardNum, int minNum, bool optional,
                                          bool includeEquip, const QString &pattern,
                                          const QList<int> &candidates, QList<int> &cards) const
{
    if (!player || !player->getAI()) return false;
    AIChoiceOptions options;
    options.reason = reason;
    if (reason == "qhstandardtuxiCARD") options.question = QStringLiteral("askForDiscard");
    if (reason == "ny_tenth_shizong") options.question = QStringLiteral("askForDiscard");
    if (reason == "chendancui") options.question = QStringLiteral("askForDiscard");
    if (reason == "ny_10th_xiaoyan") options.question = QStringLiteral("askForDiscard");
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
    if(fromIsolated && result.originalDiscardError) {
        // This exception is only a runtime-issued result of the exact fixed
        // source error fallback. It cannot authorize any Lua-selected IDs.
        if(!aiRound35OriginalQuestion(request) || request.kind!=AIRequest::Discard
            || result.decisionId!=request.decisionId || result.stateRevision!=request.stateRevision
            || result.stateRevision!=m_room.roomRuntime()->stateRevision()
            || result.originalDiscardViewer!=player->objectName()
            || result.originalDiscardReason!=reason || result.originalDiscardPattern!=pattern
            || result.originalDiscardMinimum!=minNum || result.originalDiscardMaximum!=discardNum
            || result.originalDiscardOptional!=optional || result.originalDiscardIncludeEquip!=includeEquip
            || result.originalDiscardExchange!=player->hasFlag("Global_AIDiscardExchanging"))return false;
        // No new target/card authorization: revalidate every issued physical
        // result remains in this owner's source h/e domain at this revision.
        const QList<int> hand=player->handCards();
        QSet<int> current;for(int id:hand)current.insert(id);
        if(includeEquip)for(const Card*c:player->getEquips())current.insert(c->getId());
        for(int id:result.action.selectedCardIds)if(!current.contains(id))return false;
        cards=result.action.selectedCardIds;
        if(!commitAcceptedChoiceEffects(m_room,request,result))return false;
        // Native legacy wrapper emits Pass for empty results. The Room caller
        // ignores this bool and performs its normal shortfall processing; keep
        // that final-gate distinction while committing the reached AI prefix.
        return !cards.isEmpty() || optional;
    }
    if (fromIsolated && result.kind == AIResult::Pass && optional) {
        cards.clear();
        return commitAcceptedChoiceEffects(m_room, request, result);
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
    return commitAcceptedChoiceEffects(m_room, request, result);
}

bool AiDecisionCoordinator::decideAmazingGrace(ServerPlayer *player, const QList<int> &cardIds,
                                               bool refusable, const QString &reason,
                                               int &cardId) const
{
    if (!player || !player->getAI()) return false;
    AIChoiceOptions options;
    options.reason = reason;
    if (reason == "kezhuanmanjuan" || reason == "diting" || reason == "gengdu" || reason == "zhuning" || reason == "geass" || reason == "xinshengll") options.question = QStringLiteral("askForAG");
    options.cardIds = cardIds;
    options.optional = refusable;
    AI *ai = player->getAI();
    AIResult result;
    bool fromIsolated = false;
    int legacyCardId = -1;
    AIRequest request = makeChoiceRequest(player, AIRequest::AmazingGrace, options);
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
                   }, result, &fromIsolated))
        return false;
    if (!fromIsolated) {
        if (legacyCardId < 0) return false;
        cardId = legacyCardId;
        return commitAcceptedChoiceEffects(m_room, request, result);
    }
    if (result.kind == AIResult::Pass && refusable) {
        cardId = -1;
        return commitAcceptedChoiceEffects(m_room, request, result);
    }
    if (result.kind != AIResult::Answer || result.action.selectedCardIds.size() != 1)
        return false;
    if (!cardIds.contains(result.action.selectedCardIds.first()))
        return false;
    cardId = result.action.selectedCardIds.first();
    return commitAcceptedChoiceEffects(m_room, request, result);
}

bool AiDecisionCoordinator::decideCardChosen(ServerPlayer *player, ServerPlayer *who,
                                             const QString &flags, const QString &reason,
                                             Card::HandlingMethod method, int &cardId, bool handcardVisible) const
{
    if (!player || !player->getAI() || !who) return false;
    AIChoiceOptions options;
    options.question = QStringLiteral("askForCardChosen");
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
    AIPlayerView *targetView = who == player ? &request.worldView.self : nullptr;
    if (!targetView) {
        for (AIPlayerView &view : request.worldView.players) {
            if (view.objectName == who->objectName()) { targetView = &view; break; }
        }
    }
    if (targetView && handcardVisible && who != player) {
        targetView->handVisible = true;
        targetView->knownCards.clear();
        QJsonObject permissions = request.worldView.self.canDiscard.value(who->objectName()).toObject();
        for (const Card *card : who->getHandcards()) {
            if (!card) continue;
            targetView->knownCards << makeAICardView(card);
            permissions.insert(QString::number(card->getEffectiveId()), player->canDiscard(who, card->getEffectiveId()));
        }
        request.worldView.self.canDiscard.insert(who->objectName(), permissions);
        request.choiceOptions.context.insert("handcard_visible", true);
    }
    QSet<int> disabled;
    for (const QVariant &value : player->getTag("cardChosenForAI").toList()) disabled.insert(value.toInt());
    if (targetView) {
        QList<AICardView> visible;
        if (flags.contains(QLatin1Char('e'))) visible << targetView->equips;
        if (flags.contains(QLatin1Char('j'))) visible << targetView->judgingArea;
        if (flags.contains(QLatin1Char('h'))) {
            visible << (who == player ? request.worldView.handCards : targetView->knownCards);
            if (!targetView->handVisible) request.choiceOptions.candidatesComplete = false;
        }
        for (const AICardView &card : visible) {
            if (disabled.contains(card.effectiveId)) continue;
            if (method == Card::MethodDiscard && !player->canDiscard(who, card.effectiveId)) continue;
            request.choiceOptions.cardIds << card.effectiveId;
            request.choiceOptions.cards << card;
        }
    } else {
        request.choiceOptions.candidatesComplete = false;
    }
    projectAIStrategyRandomRequest(request, m_room, player);
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
        return commitAcceptedChoiceEffects(m_room, request, result);
    }
    if (result.kind != AIResult::Answer || result.action.selectedCardIds.size() != 1)
        return false;
    cardId = result.action.selectedCardIds.first();
    if (disabled.contains(cardId)) return false;
    if (!result.action.randomCardChoiceResolved && !request.choiceOptions.cardIds.contains(cardId)) return false;
    if (method == Card::MethodDiscard && !player->canDiscard(who, cardId)) return false;
    return commitAcceptedChoiceEffects(m_room, request, result);
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
    AIRequest request = makeChoiceRequest(player, AIRequest::Yiji, options);
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
                   }, result, &fromIsolated))
        return false;
    if (!fromIsolated) {
        if (!legacyReceiver || legacyCardId < 0) return false;
        target = legacyReceiver;
        cardId = legacyCardId;
        return commitAcceptedChoiceEffects(m_room, request, result);
    }
    if (result.kind == AIResult::Pass) {
        target = nullptr;
        cardId = -1;
        return commitAcceptedChoiceEffects(m_room, request, result);
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
            return commitAcceptedChoiceEffects(m_room, request, result);
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
    std::unique_ptr<LuaRuntime::Binding> round56Binding;
    std::unique_ptr<AIRound56ProjectionScope> round56Projection;
    // The first complete source union belongs to the public Choulve question.
    // Other PlayerChosen policies keep their existing entry behavior.
    if (reason == "choulve") {
        round56Binding.reset(new LuaRuntime::Binding(m_room.roomRuntime()->lua(),false));
        round56Projection.reset(new AIRound56ProjectionScope(m_room,player,m_room.roomRuntime()->lua().rawState()));
    }
    AIChoiceOptions options;
    options.reason = reason;
    if (reason == "luochong" || reason == "choulve" || reason == "xiaoxinf_slash") options.question = QStringLiteral("askForPlayerChosen");
    if (reason == "dressing") options.question = QStringLiteral("askForPlayerChosen");
    if (reason == "peiqi_from" || reason == "peiqi_to") options.question = QStringLiteral("askForPlayerChosen");
    if (reason == "mythluochong" || reason == "tenyearmingfa" || reason == "lianzhan") options.question = QStringLiteral("askForPlayerChosen");
    if (reason == "qhfireqinyin") options.question = QStringLiteral("askForPlayerChosen");
    if (reason == "yikuxin") options.question = QStringLiteral("askForPlayerChosen");
    if (reason == "ny_10th_yijia") options.question = QStringLiteral("askForPlayerChosen");
    if (reason == "kesxchunlao") options.question = QStringLiteral("askForPlayerChosen");
    foreach (ServerPlayer *target, targets) {
        if (target)
            options.playerNames << target->objectName();
    }
    AI *ai = player->getAI();
    AIResult result;
    bool fromIsolated = false;
    ServerPlayer *legacyChoice = nullptr;
    AIRequest request = makeChoiceRequest(player, AIRequest::PlayerChosen, options);
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
                   }, result, &fromIsolated))
        return false;
    if (!fromIsolated) {
        if (!legacyChoice) return false;
        choice = legacyChoice;
        return commitAcceptedChoiceEffects(m_room, request, result);
    }
    if (result.kind == AIResult::Pass && result.action.selectedTargetNames.isEmpty()) {
        // SmartAI's absent/invalid callback returns nil without a selection RNG
        // draw. The pinned native inquiry leaves its incoming choice unchanged
        // in that case (PlayerDecisionService initializes it to targets.first()).
        // Accept the isolated semantic pass without invoking legacy fallback;
        // do not change the separate native optional/default-choice contract.
        const bool committed=commitAcceptedChoiceEffects(m_room, request, result);
        if(committed&&reason=="choulve")recordAIRound56ChoulveNativeAcceptance(m_room);
        return committed;
    }
    if (result.kind != AIResult::Answer || result.action.selectedTargetNames.size() != 1)
        return false;
    foreach (ServerPlayer *target, targets) {
        if (target && target->objectName() == result.action.selectedTargetNames.first()) {
            QString error;
            if (!AiLuaRuntime::validateDecisionEffects(request.worldView, result.strategyEffects)) return false;
            if (!stageAcceptedAIStrategyChoiceHints(m_room, request, result.strategyEffects, target, &error, false)) return false;
            if (!m_room.roomRuntime()->ai().commitStrategyEffects(request.worldView,
                nonHintStrategyEffects(result.strategyEffects), m_room.roomRuntime()->lua(), &error)) return false;
            if (!stageAcceptedAIStrategyChoiceHints(m_room, request, result.strategyEffects, target, &error)) return false;
            choice = target;
            if(reason=="choulve")recordAIRound56ChoulveNativeAcceptance(m_room);
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
    if (reason == "qhfireluanji" || reason == "#xunshi") options.question = QStringLiteral("askForPlayersChosen");
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
    AIRequest request = makeChoiceRequest(player, AIRequest::PlayersChosen, options);
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
                   }, result, &fromIsolated))
        return false;
    if (!fromIsolated) {
        if (legacyPicked.isEmpty()) return false;
        chosen = legacyPicked;
        return commitAcceptedChoiceEffects(m_room, request, result);
    }
    if (result.kind == AIResult::Pass && minNum <= 0) {
        // Callers may prefill this list with every candidate. A deliberate decline
        // must clear it, rather than leave that default looking like an AI choice.
        chosen.clear();
        return commitAcceptedChoiceEffects(m_room, request, result);
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
    return commitAcceptedChoiceEffects(m_room, request, result);
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
    AIRequest request = makeChoiceRequest(player, AIRequest::Guanxing, options);
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
                   }, result))
        return false;
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
    return commitAcceptedChoiceEffects(m_room, request, result);
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
            if (event.event() == GameReady && !m_room.externalAgent(player->objectName())) {
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
    AI *ai = player->getAI();
    AIResult result;
    AIRequest request = makeChoiceRequest(player, AIRequest::TriggerOrder, options);
    if (!runAnswer(player, request,
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
    return commitAcceptedChoiceEffects(m_room, request, result);
}

const Card *AiDecisionCoordinator::decideResponse(ServerPlayer *player, const AIRequest &request,
                                                  const QString &callbackName,
                                                  const LegacyCard &legacy) const
{
    if (!player || !player->getAI()) return nullptr;
    AIResult externalResult;
    const Card *externalCard = nullptr;
    if (externalAnswer(player, request, externalResult, [&](const AIResult &answer) {
            if (answer.kind == AIResult::Pass) return request.choiceOptions.optional;
            externalCard = responseCard(player, request, answer);
            return externalCard != nullptr;
        })) return externalCard;
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
        const AIResult result = m_room.roomRuntime()->ai().decideIsolated(request);
        traceIsolatedDecision(request, result, "proposed-response");
        if(result.hasOriginalResponseWire) {
            const auto source=commitAIRound36OriginalResponseWire(m_room,player,request,result,[&](){
                QString error;
                return m_room.roomRuntime()->ai().commitStrategyEffects(request.worldView,
                    result.strategyEffects,m_room.roomRuntime()->lua(),&error);
            });
            traceIsolatedDecision(request,result,source.accepted?"native-accepted":"native-rejected");
            // A committed source refusal/rejection is final. Never call a
            // legacy AI or re-use this old-revision request/ticket afterward.
            return source.accepted?source.card:nullptr;
        }

        // A deliberate, current decline is an isolated decision. The fault guard
        // below must never override it or count as successful isolated coverage.
        if (result.handled && result.errorCode.isEmpty() && result.kind == AIResult::Pass
            && request.choiceOptions.optional && result.decisionId == request.decisionId
            && result.stateRevision == request.stateRevision
            && aiRound44RangeRequestCurrent(m_room,request,player)) {
            QString effectsError;
            if (!m_room.roomRuntime()->ai().commitStrategyEffects(request.worldView, result.strategyEffects,
                    m_room.roomRuntime()->lua(), &effectsError)) return nullptr;
            traceIsolatedDecision(request, result, "native-accepted");
            return nullptr;
        }
        const Card *answered = responseCard(player, request, result);
        if (answered) {
            QString effectsError;
            if (!m_room.roomRuntime()->ai().commitStrategyEffects(request.worldView, result.strategyEffects,
                    m_room.roomRuntime()->lua(), &effectsError)) {
                if (answered->isVirtualCard()) const_cast<Card *>(answered)->deleteLater();
                return nullptr;
            }
            traceIsolatedDecision(request, result, "native-accepted");
            return answered;
        }
        if (result.handled && result.errorCode.isEmpty())
            traceIsolatedDecision(request, result, "native-rejected");
        traceIsolatedDecision(request, result, "fallback");
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
        || !aiRound44RangeRequestCurrent(m_room,request,player))
        return nullptr;
    const auto rescueProhibited = [&](const Card *card) {
        if (request.choiceOptions.question != QStringLiteral("askForSinglePeach")) return false;
        if (request.choiceOptions.playerNames.size() != 1) return true;
        ServerPlayer *dying = m_room.findPlayerByObjectName(request.choiceOptions.playerNames.first());
        return !dying || player->isProhibited(dying, card);
    };
    QString pattern = request.pattern;
    if (pattern.endsWith(QLatin1Char('!'))) pattern.chop(1);
    if (result.action.hasCardSpec) {
        if (!result.action.selectedCardIds.isEmpty()
            || request.choiceOptions.question == QStringLiteral("askForCardShow")
            || request.choiceOptions.question == QStringLiteral("askForPindian")) return nullptr;
        Card *card = buildSpecCard(player, request, result.action.cardSpec);
        if (!card) return nullptr;
        const AICardConversionView *ticket = responseTicket(request, result.action.cardSpec.conversionId);
        // SkillCard receipts already revalidated the authority-created concrete
        // response proxy in buildSpecCard. A bare SkillCard class cannot itself
        // match the declared Slash/Peach response expression.
        const bool classPlanning = usesClassResponsePlanning(request);
        const QJsonObject planningValue = ticket ? (classPlanning ? ticket->responseClassPlanning : ticket->responsePlanning) : QJsonObject();
        const bool plannedProxy = !planningValue.isEmpty() && (!classPlanning || planningValue.value("exists").toBool());
        bool prohibited = rescueProhibited(card);
        if (plannedProxy) {
            Card *proxy = Sanguosha->cloneCard(planningValue.value("card").toObject().value("name").toString());
            if (!proxy) { card->deleteLater(); return nullptr; }
            if (!classPlanning) proxy->setSkillName(ticket->skillName);
            if (classPlanning) {
                for (const auto &id : planningValue.value("card").toObject().value("subcards").toArray())
                    proxy->addSubcard(id.toInt());
            } else if (!card->willThrow()) proxy->addSubcards(card->getSubcards());
            prohibited |= rescueProhibited(proxy);
            proxy->deleteLater();
        }
        if (prohibited || player->isCardLimited(card, request.handlingMethod)
            || (!plannedProxy && !pattern.isEmpty() && !Sanguosha->matchPattern(pattern, player, card))) {
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
    if (request.choiceOptions.question == "askForCard" && request.pattern == "@axe"
        && request.handlingMethod == Card::MethodNone
        && request.choiceOptions.context.value("aggregate_response").toString() == "axe") {
        const auto *skill = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill("axe"));
        if (!skill || result.action.selectedCardIds.size() != 2) return nullptr;
        EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
        ResponseNativeScope responseScope(m_room, player, request.reason, request.pattern);
        ActiveSkillRequest selection;
        selection.initiator = player; selection.reason = request.reason; selection.pattern = request.pattern;
        if (!skill->canActivateRequest(selection)) return nullptr;
        for (int id : result.action.selectedCardIds) {
            const Card *material = Sanguosha->getCard(id);
            if (!material || !skill->canSelectCard(selection, material)) return nullptr;
            selection.selectedCardIds << id;
        }
        if (!skill->cardSelectionFeasible(selection)) return nullptr;
        const Card *card = skill->createCard(selection);
        if (!card) return nullptr;
        if (!card->isVirtualCard() || card->getSubcards() != selection.selectedCardIds) {
            if (card->isVirtualCard()) const_cast<Card *>(card)->deleteLater();
            return nullptr;
        }
        CardLifetimeManager &lifetime = globalCardLifetimeManager();
        if (lifetime.mode() == CardLifetimeMode::ObserveOnly) const_cast<Card *>(card)->deleteLater();
        else lifetime.requestNativeDelete(const_cast<Card *>(card));
        return card;
    }
    if (result.action.selectedCardIds.size() != 1) return nullptr;
    const int cardId = result.action.selectedCardIds.first();
    if(request.choiceOptions.question=="askForCard"&&request.pattern=="@@sijyu_fengyi") {
        // This physical source answer is authenticated by the real retrial frame,
        // not an @@ pattern match or a virtual/ViewAs replacement.
        EngineRuntimeContextScope engineContext(*Sanguosha,&m_room);
        LuaRuntime::Binding game(m_room.roomRuntime()->lua(),false);
        return consumeAIRound47FengyiPhysical(m_room,request,player,cardId);
    }
    const bool danlvePhysical=authorizeAIRound45DanlvePhysical(m_room,request,player,cardId);
    if (!danlvePhysical && !request.choiceOptions.cardIds.contains(cardId)) return nullptr;
    if (!danlvePhysical && !player->handCards().contains(cardId)) {
        bool equipped = false;
        foreach (const Card *equip, player->getEquips()) {
            if (equip && equip->getEffectiveId() == cardId) {
                equipped = true;
                break;
            }
        }
        if (!equipped && !player->getHandPile().contains(cardId))
            return nullptr;
    }
    const Card *card = Sanguosha->getCard(cardId);
    if (!card || rescueProhibited(card) || player->isCardLimited(card, request.handlingMethod)) return nullptr;
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
    const QSet<int> handSet = qsanToSet(hand);
    for (const AICardCandidateView &candidate : request.cardCandidates) {
        // Showing/pindian/physical responses choose the viewer's hand. Equipment is
        // still available as a conversion cost, never as an arbitrary shown card.
        if (!candidate.available || candidate.limited || (physicalOnly && !handSet.contains(candidate.cardId))) continue;
        const Card *card = Sanguosha->getCard(candidate.cardId);
        if (!card) { options.candidatesComplete = false; continue; }
        options.cardIds << candidate.cardId;
        options.cards << makeAICardView(card);
    }
    if (player && question == "askForCard" && pattern == "@axe" && method == Card::MethodNone) {
        const auto *skill = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill("axe"));
        ActiveSkillRequest selection;
        selection.initiator = player; selection.reason = request.reason; selection.pattern = pattern;
        if (skill && skill->canActivateRequest(selection)) options.context.insert("aggregate_response", "axe");
    }
    request.choiceOptions = options;
    projectAIStrategyRequest(request, m_room, player);
    projectAIStrategyResponseContext(request, m_room, player, QVariant(), request.choiceOptions.context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIStrategyRound24Response(request, m_room, player, QVariant(), request.choiceOptions.context);
    projectAIStrategyRetrialContext(request, m_room, player, QVariant(), request.choiceOptions.context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIStrategyChoiceContext(request, m_room, player, QVariant(), request.choiceOptions.context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIStrategyYCZH2017(request, m_room, player, [&](const Card *card) {
        return publicDecisionCard(card, request.choiceOptions.reason != "funan");
    });
    projectAIStrategyRound21Response(request, m_room, player, request.choiceOptions.context);
    projectAIRound35TrustCardDefault(request, m_room, player);
    projectAIRound49Xinhuo(request, m_room, player);
    projectAIRound49Guhuo(request, m_room, player);
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
    projectAIStrategyChoiceContext(request, m_room, player, QVariant(), request.choiceOptions.context,
        [](const Card *card) { return publicDecisionCard(card); });
    projectAIRound33AmazingGrace(request, m_room, player, trick, from, to);
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
    if (reason == "mizhao" && requestor) {
        // The actual pindian pair was publicly logged before either card ask.
        QString target;
        for (ServerPlayer *candidate : m_room.getAlivePlayers())
            if (candidate->hasFlag("MizhaoPindianTarget")) {
                if (!target.isEmpty()) { target.clear(); break; }
                target = candidate->objectName();
            }
        if (!target.isEmpty()) request.choiceOptions.context.insert("mizhao_pindian_target", target);
    }
    projectAIRound41SkillFlagQuestion(request, m_room, player);
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
    AIResult externalResult;
    if (externalAnswer(player, request, externalResult, [&](const AIResult &answer) {
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
        })) return true;
    const QString callbackName = request.kind == AIRequest::Activate
        ? QStringLiteral("activate") : QStringLiteral("askForUseCard");
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
        result = m_room.roomRuntime()->ai().decideIsolated(request);
        traceIsolatedDecision(request, result, "proposed-play");
        if (!result.handled || !result.errorCode.isEmpty()) {
            traceIsolatedDecision(request, result, "fallback");
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
    bool accepted = applyResult(player, request, result, cardUse);
    if (accepted && route == AiRouteIsolated) {
        QString effectsError;
        accepted = commitAcceptedCardEffects(m_room, request, result, cardUse.card, &effectsError);
        if (!accepted) { cardUse.card = nullptr; cardUse.to.clear(); }
    }
    if (route == AiRouteIsolated)
        traceIsolatedDecision(request, result, accepted ? "native-plan-accepted" : "native-rejected");
    return accepted;
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
