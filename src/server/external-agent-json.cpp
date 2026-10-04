#include "external-agent-transport.h"
#include <QJsonArray>
#include <QSet>
#include <cmath>
#include <limits>

namespace {
QJsonValue json(const QString &v) { return v; }
QJsonValue json(bool v) { return v; }
QJsonValue json(int v) { return v; }
QJsonValue json(quint64 v) { return QString::number(v); }
QJsonValue json(const QJsonObject &v) { return v; }
QJsonValue json(const SkillInstanceRef &v) {
    return QJsonObject{{"ownerObjectName", v.ownerObjectName},
        {"skillName", v.key.skillName}, {"instanceID", v.key.instanceID}};
}
QJsonValue json(const AICardView &v);
QJsonValue json(const AICardPileView &v);
QJsonValue json(const AICardCandidateView &v);
QJsonValue json(const AICardConversionView &v);
QJsonValue json(const AISkillView &v);
QJsonValue json(const AIPlayerView &v);
QJsonValue json(const AIEventView &v);
QJsonValue json(const AIWorldView &v);
QJsonValue json(const AiSkillActionContext &v);
QJsonValue json(const AIChoiceOptions &v);
QJsonValue json(const AIRequest &v);
template<class T> QJsonValue json(const QList<T> &values) {
    QJsonArray result;
    for (const auto &v : values) result.append(json(v));
    return result;
}
QString key(const QString &v) { return v; }
QString key(int v) { return QString::number(v); }
template<class K, class V> QJsonValue json(const QMap<K,V> &values) {
    QJsonObject result;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        result.insert(key(it.key()), json(it.value()));
    return result;
}
QJsonValue json(const AICardView &v) {
    return QJsonObject{
        {"cardId", json(v.cardId)},
        {"effectiveId", json(v.effectiveId)},
        {"objectName", json(v.objectName)},
        {"className", json(v.className)},
        {"suit", json(v.suit)},
        {"number", json(v.number)},
        {"skillName", json(v.skillName)},
        {"typeId", json(v.typeId)},
        {"equipSlot", json(v.equipSlot)},
        {"weaponRange", json(v.weaponRange)},
        {"handlingMethod", json(v.handlingMethod)},
        {"virtualCard", json(v.virtualCard)},
        {"targetFixed", json(v.targetFixed)},
        {"damageCard", json(v.damageCard)},
        {"subcardIds", json(v.subcardIds)},
        {"red", json(v.red)},
        {"black", json(v.black)},
        {"kindOfNames", json(v.kindOfNames)}
    };
}
QJsonValue json(const AICardPileView &v) {
    return QJsonObject{
        {"name", json(v.name)},
        {"count", json(v.count)},
        {"open", json(v.open)},
        {"handPile", json(v.handPile)},
        {"cardIds", json(v.cardIds)},
        {"cards", json(v.cards)}
    };
}
QJsonValue json(const AICardCandidateView &v) {
    return QJsonObject{
        {"candidateId", json(v.candidateId)},
        {"cardId", json(v.cardId)},
        {"available", json(v.available)},
        {"limited", json(v.limited)},
        {"jilei", json(v.jilei)},
        {"targetFixed", json(v.targetFixed)},
        {"maxVotes", json(v.maxVotes)},
        {"legalTargets", json(v.legalTargets)},
        {"affectedTargetsKnown", json(v.affectedTargetsKnown)},
        {"affectedTargets", json(v.affectedTargets)},
        {"targetCombinations", json(v.targetCombinations)},
        {"feasibleWithNoTarget", json(v.feasibleWithNoTarget)},
        {"completeCoverage", json(v.completeCoverage)}
    };
}
QJsonValue json(const AICardConversionView &v) {
    return QJsonObject{
        {"conversionId", json(v.conversionId)},
        {"name", json(v.name)},
        {"className", json(v.className)},
        {"kindOfNames", json(v.kindOfNames)},
        {"suit", json(v.suit)},
        {"number", json(v.number)},
        {"activationRef", json(v.activationRef)},
        {"sourceRef", json(v.sourceRef)},
        {"activationQuotaAvailable", json(v.activationQuotaAvailable)},
        {"sourceQuotaAvailable", json(v.sourceQuotaAvailable)},
        {"costCount", json(v.costCount)},
        {"eligibleSubcardIds", json(v.eligibleSubcardIds)},
        {"subcardIds", json(v.subcardIds)},
        {"available", json(v.available)},
        {"targetFixed", json(v.targetFixed)},
        {"feasibleWithNoTarget", json(v.feasibleWithNoTarget)},
        {"completeCoverage", json(v.completeCoverage)},
        {"legalTargets", json(v.legalTargets)},
        {"affectedTargetsKnown", json(v.affectedTargetsKnown)},
        {"affectedTargets", json(v.affectedTargets)},
        {"targetCombinations", json(v.targetCombinations)},
        {"maxVotes", json(v.maxVotes)}
    };
}
QJsonValue json(const AISkillView &v) {
    return QJsonObject{
        {"skillName", json(v.skillName)},
        {"instanceId", json(v.instanceId)},
        {"source", json(v.source)},
        {"invalid", json(v.invalid)},
        {"hasAmountOverride", json(v.hasAmountOverride)},
        {"amount", json(v.amount)},
        {"hasPrivateState", json(v.hasPrivateState)},
        {"hasViewAsSkill", json(v.hasViewAsSkill)},
        {"skillClasses", json(v.skillClasses)},
        {"frequency", json(v.frequency)},
        {"lordSkill", json(v.lordSkill)},
        {"attachedLordSkill", json(v.attachedLordSkill)},
        {"lordSkillEffective", json(v.lordSkillEffective)},
        {"state", json(v.state)},
        {"correctState", json(v.correctState)}
    };
}
QJsonValue json(const AIPlayerView &v) {
    return QJsonObject{
        {"objectName", json(v.objectName)},
        {"seat", json(v.seat)},
        {"hp", json(v.hp)},
        {"maxHp", json(v.maxHp)},
        {"handcardCount", json(v.handcardCount)},
        {"phase", json(v.phase)},
        {"alive", json(v.alive)},
        {"dead", json(v.dead)},
        {"removed", json(v.removed)},
        {"kongcheng", json(v.kongcheng)},
        {"wounded", json(v.wounded)},
        {"faceUp", json(v.faceUp)},
        {"chained", json(v.chained)},
        {"kingdom", json(v.kingdom)},
        {"role", json(v.role)},
        {"roleRevealed", json(v.roleRevealed)},
        {"roleVisible", json(v.roleVisible)},
        {"controller", json(v.controller)},
        {"generalName", json(v.generalName)},
        {"general2Name", json(v.general2Name)},
        {"equips", json(v.equips)},
        {"judgingArea", json(v.judgingArea)},
        {"publicMarks", json(v.publicMarks)},
        {"skills", json(v.skills)},
        {"knownCards", json(v.knownCards)},
        {"handVisible", json(v.handVisible)},
        {"privateFlags", json(v.privateFlags)},
        {"privateFlagsVisible", json(v.privateFlagsVisible)},
        {"skippedPhases", json(v.skippedPhases)},
        {"activeArmorName", json(v.activeArmorName)},
        {"armorEffectKnown", json(v.armorEffectKnown)},
        {"piles", json(v.piles)},
        {"displayCards", json(v.displayCards)},
        {"maxCards", json(v.maxCards)},
        {"hujia", json(v.hujia)},
        {"attackRange", json(v.attackRange)},
        {"gender", json(v.gender)},
        {"lord", json(v.lord)},
        {"equipSlots", json(v.equipSlots)}
    };
}
QJsonValue json(const AIEventView &v) {
    return QJsonObject{
        {"sequence", json(v.sequence)},
        {"revision", json(v.revision)},
        {"triggerEvent", json(v.triggerEvent)},
        {"kind", json(v.kind)},
        {"from", json(v.from)},
        {"to", json(v.to)},
        {"targets", json(v.targets)},
        {"cardName", json(v.cardName)},
        {"cardClass", json(v.cardClass)},
        {"cardSkill", json(v.cardSkill)},
        {"chain", json(v.chain)},
        {"transfer", json(v.transfer)},
        {"byUser", json(v.byUser)},
        {"intentionSuppressed", json(v.intentionSuppressed)},
        {"details", json(v.details)},
        {"reason", json(v.reason)},
        {"cardIds", json(v.cardIds)},
        {"privateCardIds", json(v.privateCardIds)},
        {"privateViewer", json(v.privateViewer)},
        {"privateEvent", json(v.privateEvent)},
        {"amount", json(v.amount)},
        {"nature", json(v.nature)},
        {"place", json(v.place)},
        {"good", json(v.good)}
    };
}
QJsonValue json(const AIWorldView &v) {
    return QJsonObject{
        {"revision", json(v.revision)},
        {"modeId", json(v.modeId)},
        {"customRoles", json(v.customRoles)},
        {"modePolicy", json(v.modePolicy)},
        {"self", json(v.self)},
        {"players", json(v.players)},
        {"handCards", json(v.handCards)},
        {"discardPile", json(v.discardPile)},
        {"playerOrder", json(v.playerOrder)},
        {"alivePlayerOrder", json(v.alivePlayerOrder)},
        {"currentPlayer", json(v.currentPlayer)},
        {"currentPhase", json(v.currentPhase)},
        {"distances", json(v.distances)},
        {"distanceScope", json(v.distanceScope)},
        {"events", json(v.events)}
    };
}
QJsonValue json(const AiSkillActionContext &v) {
    return QJsonObject{
        {"activationRef", json(v.activationRef)},
        {"sourceRef", json(v.sourceRef)},
        {"activationQuotaAvailable", json(v.activationQuotaAvailable)},
        {"sourceQuotaAvailable", json(v.sourceQuotaAvailable)}
    };
}
QJsonValue json(const AIChoiceOptions &v) {
    return QJsonObject{
        {"reason", json(v.reason)},
        {"question", json(v.question)},
        {"choices", json(v.choices)},
        {"cardIds", json(v.cardIds)},
        {"cards", json(v.cards)},
        {"candidatesComplete", json(v.candidatesComplete)},
        {"context", json(v.context)},
        {"playerNames", json(v.playerNames)},
        {"defaultChoice", json(v.defaultChoice)},
        {"hasDefaultChoice", json(v.hasDefaultChoice)},
        {"optional", json(v.optional)},
        {"minCount", json(v.minCount)},
        {"maxCount", json(v.maxCount)}
    };
}
QJsonValue json(const AIRequest &v) {
    return QJsonObject{
        {"kind", json(int(v.kind))},
        {"decisionId", json(v.decisionId)},
        {"stateRevision", json(v.stateRevision)},
        {"viewerObjectName", json(v.viewerObjectName)},
        {"reason", json(int(v.reason))},
        {"pattern", json(v.pattern)},
        {"prompt", json(v.prompt)},
        {"handlingMethod", json(int(v.handlingMethod))},
        {"worldView", json(v.worldView)},
        {"hasSkillActionContext", json(v.hasSkillActionContext)},
        {"skillActionContext", json(v.skillActionContext)},
        {"skillActions", json(v.skillActions)},
        {"choiceOptions", json(v.choiceOptions)},
        {"cardCandidates", json(v.cardCandidates)},
        {"cardConversions", json(v.cardConversions)},
        {"conversionsEnumerated", json(v.conversionsEnumerated)}
    };
}
bool fields(const QJsonObject &o, const QSet<QString> &allowed) {
    for (auto it = o.begin(); it != o.end(); ++it) if (!allowed.contains(it.key())) return false;
    return true;
}
bool integer(const QJsonValue &v, int &out) {
    if (!v.isDouble()) return false;
    const double n = v.toDouble();
    if (!std::isfinite(n) || std::floor(n) != n || n < std::numeric_limits<int>::min()
        || n > std::numeric_limits<int>::max()) return false;
    out = int(n); return true;
}
bool stamp(const QJsonValue &v, quint64 &out) {
    if (!v.isString()) return false;
    const QString s = v.toString();
    bool ok = false; out = s.toULongLong(&ok);
    return ok && QString::number(out) == s;
}
bool text(const QJsonValue &v, QString &out) {
    if (!v.isString() || v.toString().size() > 65536) return false;
    out = v.toString(); return true;
}
bool integers(const QJsonValue &v, QList<int> &out) {
    if (!v.isArray() || v.toArray().size() > 2048) return false;
    for (const auto &item : v.toArray()) { int n; if (!integer(item,n)) return false; out << n; }
    return true;
}
bool strings(const QJsonValue &v, QStringList &out) {
    if (!v.isArray() || v.toArray().size() > 64) return false;
    for (const auto &item : v.toArray()) { QString s; if (!text(item,s)) return false; out << s; }
    return true;
}
bool reference(const QJsonValue &value, SkillInstanceRef &out) {
    if (!value.isObject()) return false;
    const auto v = value.toObject();
    return fields(v,{"ownerObjectName","skillName","instanceID"})
        && text(v["ownerObjectName"],out.ownerObjectName)
        && text(v["skillName"],out.key.skillName) && integer(v["instanceID"],out.key.instanceID);
}
} // namespace

QJsonObject externalAgentRequestJson(const AIRequest &request) {
    // Only the already projected value DTO crosses; no Room/Lua/native object lookup.
    return json(request).toObject();
}

bool externalAgentResultJson(const QJsonObject &o, AIResult &result) {
    AIResult r;
    if (!fields(o,{"decisionId","stateRevision","kind","action"})
        || !stamp(o["decisionId"],r.decisionId) || !stamp(o["stateRevision"],r.stateRevision)
        || !o["kind"].isString() || !o["action"].isObject()) return false;
    const auto kind = o["kind"].toString();
    if (kind == "pass") r.kind = AIResult::Pass;
    else if (kind == "answer") r.kind = AIResult::Answer;
    else if (kind == "useCard") r.kind = AIResult::UseCard;
    else return false;
    const auto a = o["action"].toObject();
    if (!fields(a,{"candidateId","useCardId","selectedCardIds","bottomCardIds",
                   "selectedTargetNames","userString","cardSpec","skillActionContext"})) return false;
    auto &out = r.action;
    if (a.contains("candidateId") && !integer(a["candidateId"],out.candidateId)) return false;
    if (a.contains("useCardId") && !integer(a["useCardId"],out.useCardId)) return false;
    if (a.contains("selectedCardIds") && !integers(a["selectedCardIds"],out.selectedCardIds)) return false;
    if (a.contains("bottomCardIds") && !integers(a["bottomCardIds"],out.bottomCardIds)) return false;
    if (a.contains("selectedTargetNames") && !strings(a["selectedTargetNames"],out.selectedTargetNames)) return false;
    if (a.contains("userString") && !text(a["userString"],out.userString)) return false;
    if (a.contains("cardSpec")) {
        if (!a["cardSpec"].isObject()) return false;
        const auto c = a["cardSpec"].toObject();
        if (!fields(c,{"name","skillName","suit","number","conversionId","subcardIds"})
            || !text(c["name"],out.cardSpec.name) || !text(c["skillName"],out.cardSpec.skillName)
            || !integer(c["suit"],out.cardSpec.suit) || !integer(c["number"],out.cardSpec.number)
            || !integer(c["conversionId"],out.cardSpec.conversionId)
            || !integers(c["subcardIds"],out.cardSpec.subcardIds)) return false;
        out.hasCardSpec = true;
    }
    if (a.contains("skillActionContext")) {
        if (!a["skillActionContext"].isObject()) return false;
        const auto c = a["skillActionContext"].toObject();
        if (!fields(c,{"activationRef","sourceRef"})
            || !reference(c["activationRef"],out.skillActionContext.activationRef)
            || !reference(c["sourceRef"],out.skillActionContext.sourceRef)) return false;
        out.hasSkillActionContext = true; // quota booleans are never trusted from a client
    }
    // An action must have one unambiguous source.
    if (int(out.useCardId >= 0) + int(out.hasCardSpec) + int(out.hasSkillActionContext) > 1) return false;
    r.handled = true; result = r; return true;
}
