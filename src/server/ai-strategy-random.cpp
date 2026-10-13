#include "ai-strategy-random.h"
#include "ai-strategy-room-ledger.h"
#include "ai-strategy-round37-choice.h"
#include "ai-runtime.h"

#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "standard.h"

#include <QJsonArray>
#include <QObject>
#include <QSet>
#include <QUuid>
#include <cmath>

namespace {

class RandomStrategyState final : public AIRoomLedger {
public:
    explicit RandomStrategyState(Room &room) : AIRoomLedger(room)
    { setObjectName(QStringLiteral("isolated_ai_random_strategy_state")); }
    quint64 generation = 0;
    QMap<QString, QSet<int>> lastCardChosenDisabled;
};

RandomStrategyState &randomState(Room &room)
{
    auto *state = dynamic_cast<RandomStrategyState *>(room.findChild<QObject *>(
        QStringLiteral("isolated_ai_random_strategy_state"), Qt::FindDirectChildrenOnly));
    if (!state) state = new RandomStrategyState(room);
    const quint64 generation = room.roomRuntime()->ai().lua().generation();
    if (state->generation != generation) {
        state->generation = generation;
        state->lastCardChosenDisabled.clear();
    }
    return *state;
}

bool requestTarget(Room &room, ServerPlayer *viewer, const AIRequest &request,
                   ServerPlayer *&target, QString &flags)
{
    if (!viewer || viewer->getRoom() != &room || request.kind != AIRequest::CardChosen
        || request.viewerObjectName != viewer->objectName()
        || request.choiceOptions.playerNames.size() != 1
        || request.choiceOptions.choices.size() != 1
        || request.stateRevision != room.roomRuntime()->stateRevision()) return false;
    target = room.findPlayerByObjectName(request.choiceOptions.playerNames.first(), true);
    flags = request.choiceOptions.choices.first();
    if (!target || flags.isEmpty()) return false;
    for (QChar zone : flags) if (zone != QLatin1Char('h') && zone != QLatin1Char('e')
        && zone != QLatin1Char('j')) return false;
    const QJsonObject &context = request.choiceOptions.context;
    if (context.value("who").toString() != target->objectName()
        || context.value("flags").toString() != flags
        || !context.value("method").isDouble()
        || context.value("method").toDouble() != int(request.handlingMethod)) return false;
    return true;
}

QList<int> eligibleCards(ServerPlayer *viewer, ServerPlayer *target,
                         const QString &flags, Card::HandlingMethod method)
{
    QSet<int> disabled;
    for (const QVariant &value : viewer->getTag("cardChosenForAI").toList())
        disabled.insert(value.toInt());
    QList<int> ids;
    // Match getCardRandomly's exact native getCards order. In particular, do
    // not sort hidden IDs or move public cards ahead of hidden hand members.
    for (const Card *card : target->getCards(flags)) {
        if (!card) continue;
        const int id = card->getEffectiveId();
        if (id < 0 || disabled.contains(id)) continue;
        if (method == Card::MethodDiscard && !viewer->canDiscard(target, id)) continue;
        ids << id;
    }
    return ids;
}

bool originalRandomQuestion(const AIRequest &request)
{
    const auto hegemony=request.worldView.strategyFacts.value("hegemony").toObject();
    return request.kind==AIRequest::CardChosen
        && request.choiceOptions.question==QStringLiteral("askForCardChosen")
        && hegemony.value("enabled")==QJsonValue(true)
        && hegemony.value("original_overrides_loaded")==QJsonValue(true);
}
QList<int> originalFullCards(ServerPlayer *target,const QString &flags)
{
    QList<int> ids;
    for(const Card *card:target->getCards(flags)) {
        if(!card || card->getEffectiveId()<0)return {};
        ids << card->getEffectiveId();
    }
    return ids;
}

bool isSubset(const QString &zones, const QString &flags)
{
    if (zones.isEmpty()) return false;
    for (QChar zone : zones) if (!flags.contains(zone)) return false;
    return true;
}

bool planningKind(AIRequest::DecisionKind kind)
{
    return kind == AIRequest::Activate || kind == AIRequest::UseCard
        || kind == AIRequest::AmazingGrace || kind == AIRequest::RespondCard;
}

bool selectedHandSample(const AIRequest &request)
{
    return request.kind == AIRequest::CardChosen
        && request.choiceOptions.question == QStringLiteral("askForCardChosen")
        && request.choiceOptions.reason == QStringLiteral("yj_tuixinzhifu")
        && request.choiceOptions.choices.size() == 1
        && request.choiceOptions.choices.first().contains(QLatin1Char('h'));
}

bool sourceFirstXuanfeng(const AIRequest &request, const QString &flags)
{
    return request.kind == AIRequest::CardChosen
        && request.choiceOptions.question == QStringLiteral("askForCardChosen")
        && request.choiceOptions.reason == QStringLiteral("xuanfeng")
        && flags == QStringLiteral("he")
        && request.handlingMethod == Card::MethodDiscard;
}

bool sourceFirstLiwei(const AIRequest &request, const QString &flags,
                      const ServerPlayer *target)
{
    // kexianbao.lua:2506 omits the optional method: the native default is
    // MethodNone, not MethodDiscard. Only the source's face-free small-hand
    // branch is covered; three or more cards would inspect a private flag.
    return request.kind == AIRequest::CardChosen
        && request.choiceOptions.question == QStringLiteral("askForCardChosen")
        && request.choiceOptions.reason == QStringLiteral("kejiexianliwei")
        && flags == QStringLiteral("he")
        && request.handlingMethod == Card::MethodNone
        && target && target->getEquips().isEmpty()
        && target->getHandcardNum() >= 1 && target->getHandcardNum() <= 2;
}

QList<int> fullHand(ServerPlayer *target)
{
    QList<int> ids;
    for (const Card *card : target->getHandcards()) {
        if (!card || card->getEffectiveId() < 0) return {};
        ids << card->getEffectiveId();
    }
    return ids;
}

} // namespace

void projectAIStrategyTuixinzhifuThirdPassRequest(AIRequest &request, Room &room,
                                                ServerPlayer *viewer)
{
    request.choiceOptions.context.remove("yongjian_third_pass");
    ServerPlayer *target = nullptr;
    QString flags;
    if (!requestTarget(room, viewer, request, target, flags)
        || request.choiceOptions.question != QStringLiteral("askForCardChosen")
        || request.choiceOptions.reason != QStringLiteral("yj_tuixinzhifu")
        || flags != QStringLiteral("hej") || target == viewer) return;
    QJsonObject domain{{"known", false}, {"target", target->objectName()}};
    request.choiceOptions.context.insert("yongjian_third_pass", domain);
    // Do not infer disclosure from a live flag or from a native card's face.
    // Only identities whose descriptors were already authorized in this request
    // may participate. A partial hand must not produce a partial ordered list.
    const AIPlayerView *view = nullptr;
    for (const AIPlayerView &candidate : request.worldView.players)
        if (candidate.objectName == target->objectName()) { view = &candidate; break; }
    if (!view) return;
    QSet<int> disclosed;
    for (const auto *rows : {&view->knownCards, &view->equips, &view->judgingArea})
        for (const AICardView &card : *rows)
            if (card.effectiveId >= 0) disclosed.insert(card.effectiveId);
    QJsonArray ordered;
    // Exact ServerPlayer::getCards sequence h, e, j. Unlike the offered domain,
    // source getCardRandomly(no_dis=nil) does not filter canDiscard. Disabled
    // IDs are filtered lazily by Lua against its original remembered cache.
    for (const Card *card : target->getCards(QStringLiteral("hej"))) {
        if (!card || card->getEffectiveId() < 0 || !disclosed.contains(card->getEffectiveId())) return;
        ordered.append(card->getEffectiveId());
    }
    if (request.stateRevision != room.roomRuntime()->stateRevision()) return;
    domain.insert("known", true);
    domain.insert("ordered_card_ids", ordered);
    request.choiceOptions.context.insert("yongjian_third_pass", domain);
}

void projectAIStrategyRandomRequest(AIRequest &request, Room &room, ServerPlayer *viewer)
{
    if (request.kind != AIRequest::CardChosen) return;
    request.randomCardChoices.clear();
    request.choiceOptions.context.remove("random_card_choices");
    projectAIStrategyTuixinzhifuThirdPassRequest(request, room, viewer);
    ServerPlayer *target = nullptr;
    QString flags;
    if (!requestTarget(room, viewer, request, target, flags) || target == viewer) return;
    EngineRuntimeContextScope contextScope(*Sanguosha, &room);
    LuaRuntime::Binding luaBinding(room.roomRuntime()->lua(), false);
    QJsonObject choices;
    // The donor asks for "h" inside hand-priority branches and the original
    // flags in its final fallback. Other subsets are never authorized here.
    QStringList zones{flags};
    if (flags.contains(QLatin1Char('h')) && flags != QStringLiteral("h")) zones << QStringLiteral("h");
    for (const QString &zone : zones) {
        const QList<int> ids = eligibleCards(viewer, target, zone, request.handlingMethod);
        // UUID uses no Room decision RNG, preserving the donor's lazy draw.
        const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        request.randomCardChoices.insert(token, ids);
        choices.insert(zone, QJsonObject{{"token", token}, {"count", ids.size()}});
    }
    // Fixed Original heg-common1814-1829 samples the FULL native order,
    // including disabled cards. No identity or face crosses this boundary.
    if(originalRandomQuestion(request))for(const QString &zone:zones) {
        const auto ids=originalFullCards(target,zone);
        const QString token=QStringLiteral("isolated-original-random:")
            +QUuid::createUuid().toString(QUuid::WithoutBraces);
        request.randomCardChoices.insert(token,ids);
        choices.insert(QStringLiteral("original:")+zone,QJsonObject{
            {"token",token},{"count",ids.size()},{"original_full_domain",true}});
    }
    // Xuanfeng's source callback selects hand[1], not getCardRandomly. Retain
    // the original full hand order, including a disabled/illegal first card;
    // the final CardChosen authority gate must reject that choice if needed.
    // The domain discloses no card IDs, faces or private first-card flags.
    if (sourceFirstXuanfeng(request, flags)) {
        const QList<int> ids = fullHand(target);
        if (ids.size() == target->getHandcardNum()) {
            const QString token = QStringLiteral("isolated-xuanfeng-first:")
                + QUuid::createUuid().toString(QUuid::WithoutBraces);
            request.randomCardChoices.insert(token, ids);
            choices.insert(QStringLiteral("xuanfeng_first_hand"), QJsonObject{
                {"token", token}, {"count", ids.size()}, {"source_first", true}});
        }
    }
    if (sourceFirstLiwei(request, flags, target)) {
        const QList<int> ids = fullHand(target);
        if (ids.size() == target->getHandcardNum()) {
            const QString token = QStringLiteral("isolated-kejiexianliwei-first:")
                + QUuid::createUuid().toString(QUuid::WithoutBraces);
            request.randomCardChoices.insert(token, ids);
            choices.insert(QStringLiteral("kejiexianliwei_first_hand"), QJsonObject{
                {"token", token}, {"count", ids.size()}, {"source_first", true}});
        }
    }
    if (request.stateRevision != room.roomRuntime()->stateRevision()) {
        request.randomCardChoices.clear();
        return;
    }
    request.choiceOptions.context.insert("random_card_choices", choices);
    if (selectedHandSample(request)) projectAIStrategyRandomPlanningRequest(request, room, viewer);
}

bool resolveAIStrategyRandomChoice(Room &room, ServerPlayer *viewer,
                                   const AIRequest &request, AIResult &result)
{
    const QJsonObject choice = result.action.randomCardChoice;
    if (choice.isEmpty()) return true;
    ServerPlayer *target = nullptr;
    QString flags;
    if (!requestTarget(room, viewer, request, target, flags) || target == viewer
        || !result.handled || !result.errorCode.isEmpty() || result.kind != AIResult::Answer
        || result.decisionId != request.decisionId || result.stateRevision != request.stateRevision
        || result.action.randomCardChoiceResolved
        || !result.action.selectedCardIds.isEmpty() || !result.action.bottomCardIds.isEmpty()
        || !result.action.selectedTargetNames.isEmpty() || !result.action.userString.isEmpty()
        || !result.action.legacyCardString.isEmpty() || result.action.hasCardSpec
        || result.action.hasSkillActionContext || result.action.useCardId != -1
        || result.action.candidateId != -1 || choice.size() != 3) return false;
    const QJsonValue tokenValue = choice.value("token");
    const QJsonValue indexValue = choice.value("index");
    const QJsonValue avoidValue = choice.value("avoid_wounded_enemy_silver_lion");
    if (!tokenValue.isString() || !indexValue.isDouble() || !avoidValue.isBool()) return false;
    const QString token = tokenValue.toString();
    if(token.startsWith(QStringLiteral("isolated-original-sd-selected:")))return resolveAIRound37SD(room,viewer,request,result);
    if (token.startsWith(QStringLiteral("isolated-hand-selected:"))) {
        if (!selectedHandSample(request) || indexValue.toDouble() != 1 || avoidValue.toBool()
            || !request.conversionLedger) return false;
        auto it = request.conversionLedger->handSampleReceipts.find(token);
        if (it == request.conversionLedger->handSampleReceipts.end()) return false;
        auto &receipt = it.value();
        if (receipt.consumed || receipt.decisionId != request.decisionId
            || receipt.revision != request.stateRevision || receipt.viewer != viewer->objectName()
            || receipt.target != target->objectName() || fullHand(target) != receipt.domain
            || !receipt.domain.contains(receipt.selectedId)) return false;
        for (const QVariant &value : viewer->getTag("cardChosenForAI").toList())
            if (value.toInt() == receipt.selectedId) return false;
        if (request.handlingMethod == Card::MethodDiscard && !viewer->canDiscard(target, receipt.selectedId)) return false;
        receipt.consumed = true;
        result.action.selectedCardIds << receipt.selectedId;
        result.action.randomCardChoiceResolved = true;
        return true;
    }
    const auto authorized = request.randomCardChoices.constFind(token);
    if (token.isEmpty() || authorized == request.randomCardChoices.cend()) return false;
    const QList<int> &ids = authorized.value();
    const double ordinal = indexValue.toDouble();
    if (!std::isfinite(ordinal) || ordinal != std::floor(ordinal)
        || ordinal < 1 || ordinal > ids.size()) return false;
    const QJsonObject projected = request.choiceOptions.context.value("random_card_choices").toObject();
    QString zones;
    bool sourceFirst = false;
    bool originalFull = false;
    for (auto it = projected.constBegin(); it != projected.constEnd(); ++it) {
        const QJsonObject row = it.value().toObject();
        if (row.value("token").toString() == token) {
            if (!zones.isEmpty() || row.value("count").toDouble(-1) != ids.size()) return false;
            zones = it.key();
            originalFull=zones.startsWith(QStringLiteral("original:"));
            if(originalFull) {
                if(!originalRandomQuestion(request)||row.size()!=3
                    ||row.value("original_full_domain")!=QJsonValue(true)
                    ||!token.startsWith(QStringLiteral("isolated-original-random:")))return false;
                zones=zones.mid(9);
            }
            sourceFirst = zones == QStringLiteral("xuanfeng_first_hand")
                || zones == QStringLiteral("kejiexianliwei_first_hand");
            if (sourceFirst && (row.size() != 3 || row.value("source_first") != QJsonValue(true))) return false;
        }
    }
    if (sourceFirst) {
        const bool admitted = zones == QStringLiteral("xuanfeng_first_hand")
            ? sourceFirstXuanfeng(request, flags)
                && token.startsWith(QStringLiteral("isolated-xuanfeng-first:"))
            : sourceFirstLiwei(request, flags, target)
                && token.startsWith(QStringLiteral("isolated-kejiexianliwei-first:"));
        if (!admitted || ordinal != 1 || avoidValue.toBool()) return false;
    } else if (!isSubset(zones, flags)) return false;
    EngineRuntimeContextScope contextScope(*Sanguosha, &room);
    LuaRuntime::Binding luaBinding(room.roomRuntime()->lua(), false);
    // Some lock/tag changes do not advance the revision. Recheck the entire
    // ordered domain, not merely the selected ID, before using its ordinal.
    if (sourceFirst) {
        if (fullHand(target) != ids || ids.size() != target->getHandcardNum()) return false;
    } else if(originalFull) {
        if(originalFullCards(target,zones)!=ids)return false;
    } else if (eligibleCards(viewer, target, zones, request.handlingMethod) != ids) return false;
    int index = int(ordinal) - 1;
    const Card *selected = Sanguosha->getCard(ids.at(index));
    if (!selected) return false;
    // Enemy is the viewer's strategy judgment carried as a boolean, not another
    // player's hidden true role. Wound/armor prerequisites are rechecked live.
    if (avoidValue.toBool()) {
        if (!target->isWounded() || !target->hasArmorEffect(QStringLiteral("SilverLion"))) return false;
        if (originalFull ? selected==target->getArmor() : selected->isKindOf("SilverLion")) {
            if (index + 1 < ids.size()) ++index;
            else if (index > 0) --index;
        }
    }
    if (request.stateRevision != room.roomRuntime()->stateRevision()) return false;
    result.action.selectedCardIds << ids.at(index);
    result.action.randomCardChoiceResolved = true;
    // Keep the already-validated opaque payload for diagnostics; the native-only
    // resolved bit is the caller's final gate, never payload presence alone.
    // the hidden ID is never appended to a serialized request candidate list.
    return true;
}

void projectAIStrategyRandomPlanningRequest(AIRequest &request, Room &room,
                                           ServerPlayer *viewer)
{
    if (!viewer || viewer->getRoom() != &room || !(planningKind(request.kind) || selectedHandSample(request))
        || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()
        || request.stateRevision != room.roomRuntime()->stateRevision()) return;
    EngineRuntimeContextScope contextScope(*Sanguosha, &room);
    LuaRuntime::Binding luaBinding(room.roomRuntime()->lua(), false);
    // Remove only this purpose's old capabilities when projection is refreshed.
    // CardChosen's independently validated token domain is not interchangeable.
    const auto previous = request.worldView.strategyFacts.value("random_hand_discard").toObject();
    for (auto it = previous.constBegin(); it != previous.constEnd(); ++it)
        request.randomCardChoices.remove(it.value().toObject().value("token").toString());
    QJsonObject projected;
    for (ServerPlayer *target : room.getAlivePlayers()) {
        if (target == viewer || (selectedHandSample(request)
            && request.choiceOptions.playerNames != QStringList{target->objectName()})) continue;
        const QList<int> ids = fullHand(target);
        if (ids.size() != target->getHandcardNum()) continue;
        const QString token = QStringLiteral("isolated-hand-discard:")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        request.randomCardChoices.insert(token, ids);
        projected.insert(target->objectName(), QJsonObject{{"token", token}, {"count", ids.size()}});
    }
    if (request.stateRevision != room.roomRuntime()->stateRevision()) {
        for (auto it = projected.constBegin(); it != projected.constEnd(); ++it)
            request.randomCardChoices.remove(it.value().toObject().value("token").toString());
        request.worldView.strategyFacts.remove("random_hand_discard");
        return;
    }
    request.worldView.strategyFacts.insert("random_hand_discard", projected);
}

QJsonObject resolveAIStrategyRandomPlanningQuery(Room &room, ServerPlayer *viewer,
                                                const AIRequest &request,
                                                const QJsonObject &query)
{
    const auto reject = [](const char *message) {
        return QJsonObject{{"error", QString::fromLatin1(message)}, {"key", "random_hand_discard"}};
    };
    if (!viewer || viewer->getRoom() != &room || !(planningKind(request.kind) || selectedHandSample(request))
        || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()
        || request.stateRevision != room.roomRuntime()->stateRevision())
        return reject("stale or unauthorized hand-sample request");
    if (!request.conversionLedger || ++request.conversionLedger->queryCount > 128)
        return reject("hand-sample query budget exceeded");
    if (query.size() != 3 || query.value("kind").toString() != QStringLiteral("random_hand_discard")
        || !query.value("token").isString() || !query.value("target").isString())
        return reject("invalid hand-sample query");
    const QString token = query.value("token").toString();
    const QString name = query.value("target").toString();
    const auto row = request.worldView.strategyFacts.value("random_hand_discard").toObject().value(name).toObject();
    const auto authorized = request.randomCardChoices.constFind(token);
    if (!token.startsWith(QStringLiteral("isolated-hand-discard:"))
        || row.value("token").toString() != token || authorized == request.randomCardChoices.cend()
        || !row.value("count").isDouble() || row.value("count").toDouble(-1) != authorized.value().size())
        return reject("unknown hand-sample capability");
    ServerPlayer *target = room.findPlayerByObjectName(name);
    if (!target || target == viewer || !target->isAlive()) return reject("invalid hand-sample target");
    EngineRuntimeContextScope contextScope(*Sanguosha, &room);
    LuaRuntime::Binding luaBinding(room.roomRuntime()->lua(), false);
    const QList<int> ids = fullHand(target);
    if (ids.size() != target->getHandcardNum() || ids != authorized.value())
        return reject("hand-sample domain changed");
    QJsonObject sample{{"kind", "hand_discard_sample"}, {"token", token},
                       {"target", name}, {"empty", ids.isEmpty()}};
    if (ids.isEmpty()) return sample;
    // Player::getRandomHandCard draws before canDiscard/disabled/poison tests,
    // including singleton and rejected-target branches. Do not prefilter IDs.
    const int id = ids.at(room.roomRuntime()->rng().bounded(ids.size()));
    sample.insert("can_discard", viewer->canDiscard(target, id));
    sample.insert("disabled", randomState(room).lastCardChosenDisabled.value(viewer->objectName()).contains(id));
    // Only identities already disclosed in this exact request can be returned.
    // Sampling never promotes an unknown hand card into the viewer's knowledge.
    for (const AIPlayerView &player : request.worldView.players) {
        if (player.objectName != name) continue;
        for (const AICardView &card : player.knownCards)
            if (card.effectiveId == id) { sample.insert("known_card_id", id); break; }
        break;
    }
    if (selectedHandSample(request)) {
        ServerPlayer *authorizedTarget = nullptr;
        QString flags;
        if (!requestTarget(room, viewer, request, authorizedTarget, flags) || authorizedTarget != target)
            return reject("selected hand-sample target is not the current question");
        const QString receiptToken = QStringLiteral("isolated-hand-selected:")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        AIHandSampleReceipt receipt;
        receipt.decisionId = request.decisionId; receipt.revision = request.stateRevision;
        receipt.viewer = viewer->objectName(); receipt.target = name;
        receipt.domain = ids; receipt.selectedId = id;
        request.conversionLedger->handSampleReceipts.insert(receiptToken, receipt);
        sample.insert("selected_choice", QJsonObject{{"token", receiptToken}, {"index", 1},
            {"avoid_wounded_enemy_silver_lion", false}});
    }
    if (request.stateRevision != room.roomRuntime()->stateRevision())
        return reject("hand-sample request changed during query");
    return sample;
}

QJsonObject refreshAIStrategyDiscardPreferences(Room &room, ServerPlayer *viewer)
{
    if (!viewer || viewer->getRoom() != &room)
        return QJsonObject{{"error", "invalid discard-preference viewer"}};
    QSet<int> disabled;
    QJsonArray values;
    for (const QVariant &value : viewer->getTag("cardChosenForAI").toList()) {
        const int id = value.toInt();
        disabled.insert(id);
        values.append(id);
    }
    randomState(room).lastCardChosenDisabled.insert(viewer->objectName(), disabled);
    return QJsonObject{{"kind", "discard_preferences"}, {"disabled_ids", values}};
}
