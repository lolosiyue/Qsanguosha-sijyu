#include "ai-strategy-yczh2016.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "engine.h"
#include "skill.h"
#include <QJsonArray>
#include <QObject>
#include <QPointer>
#include <QHash>
#include <QVariantMap>

namespace {
const QString LedgerName = QStringLiteral("_IsolatedAI.YCZH2016.Hints");
class HintLedger final : public QObject {
public:
    explicit HintLedger(Room &room) : QObject(&room) { setObjectName(LedgerName); }
    QVariantMap pending;
    QVariantMap visible;
    QHash<QString, QPointer<const Card>> acceptedCards;
};
HintLedger *ledger(Room &room, bool create)
{
    // A direct child belongs to this exact Room lifetime. No global Room* cache,
    // gameplay tags, scenario callbacks, or state-revision writes are involved.
    QObject *child = room.findChild<QObject *>(LedgerName, Qt::FindDirectChildrenOnly);
    if (child) return dynamic_cast<HintLedger *>(child);
    return create ? new HintLedger(room) : nullptr;
}
QString skillForHint(const QString &name)
{
    if (name == "jiaozhaoAI") return "jiaozhao";
    if (name == "jiyuAI") return "jiyu";
    if (name == "kuangbiAI") return "kuangbi";
    return {};
}
QString entryKey(const QString &name, const QString &player)
{
    // Roster names need not exclude punctuation; length-prefix avoids collisions.
    return QString::number(name.size()) + ':' + name + player;
}
QVariantMap cardHintIdentity(const Card *card, const SkillInstanceRef &activation,
                             const SkillInstanceRef &root)
{
    if (!card || !activation.isValid() || !root.isValid()
        || card->getActivationSkillName() != activation.key.skillName
        || card->getActivationSkillInstanceId() != activation.key.instanceID
        || card->getSourceSkillName() != root.key.skillName
        || card->getSourceSkillInstanceId() != root.key.instanceID) return {};
    QVariantList costs; for (int id : card->getSubcards()) costs.append(id);
    const SkillCard *skillCard = qobject_cast<const SkillCard *>(card->getRealCard());
    return {{"activation_owner", activation.ownerObjectName},
        {"activation_skill", activation.key.skillName}, {"activation_id", activation.key.instanceID},
        {"root_owner", root.ownerObjectName}, {"root_skill", root.key.skillName}, {"root_id", root.key.instanceID},
        {"name", card->objectName()}, {"class", card->getClassName()},
        {"skill", card->getSkillName()}, {"suit", int(card->getSuit())}, {"number", card->getNumber()},
        {"costs", costs}, {"user_string", skillCard ? skillCard->getUserString() : QString()}};
}
QJsonObject failure(const QString &message) { return {{"error", message}}; }
}

bool stageAIStrategyPlayerHints(Room &room, const AIRequest &request,
                                const QJsonArray &effects, QString *error, bool commit)
{
    const auto reject = [error](const QString &message) { if (error) *error = message; return false; };
    ServerPlayer *source = room.findPlayerByObjectName(request.viewerObjectName, true);
    if (!source || request.decisionId == 0) return reject("invalid accepted hint source");
    QVariantMap additions;
    for (const QJsonValue &value : effects) {
        if (!value.isObject()) return reject("hint is not an object");
        const QJsonObject effect = value.toObject();
        if (effect.value("kind").toString() != "ai_player_hint") continue;
        const QString name = effect.value("name").toString();
        const QString skill = skillForHint(name);
        const QString target = effect.value("player").toString();
        if (skill.isEmpty() || effect.value("source").toString() != source->objectName()
            || !room.findPlayerByObjectName(target, true)) return reject("hint source, name, or target is not authorized");
        if (skill == "jiaozhao") {
            if (request.kind != AIRequest::PlayerChosen || request.choiceOptions.reason != skill)
                return reject("Jiaozhao hint needs its accepted player choice");
        } else if (request.kind != AIRequest::Activate && request.kind != AIRequest::UseCard) {
            return reject("skill-use hint needs an accepted card action");
        }
        const QString key = entryKey(name, target);
        additions.insert(key, QVariantMap{{"name", name}, {"source", source->objectName()},
            {"target", target}, {"skill", skill}, {"decision", QString::number(request.decisionId)}});
    }
    if (additions.isEmpty()) return true;
    HintLedger *state = ledger(room, false);
    if (!state && room.findChild<QObject *>(LedgerName, Qt::FindDirectChildrenOnly))
        return reject("hint ledger name collision");
    QVariantMap pending = state ? state->pending : QVariantMap();
    // A newer accepted decision replaces all older pending guesses by this
    // source for that skill, even when its selected target has changed.
    for (const QVariant &value : additions) {
        const QVariantMap row = value.toMap();
        for (auto it = pending.begin(); it != pending.end();) {
            const QVariantMap old = it.value().toMap();
            if (old.value("source") == row.value("source") && old.value("skill") == row.value("skill")) it = pending.erase(it);
            else ++it;
        }
    }
    for (auto it = additions.cbegin(); it != additions.cend(); ++it) pending.insert(it.key(), it.value());
    const int bound = 3 * room.getAllPlayers(true).size();
    if (pending.size() > bound) return reject("hint pending roster bound exceeded");
    if (!commit) return true;
    if (!state) state = ledger(room, true);
    for (auto it = state->acceptedCards.begin(); it != state->acceptedCards.end();) {
        if (!pending.contains(it.key()) || additions.contains(it.key())) it = state->acceptedCards.erase(it);
        else ++it;
    }
    state->pending = pending;
    return true;
}

void witnessAIStrategyPlayerHints(Room &room, quint64 decisionId, const ServerPlayer *source,
                                  const QString &skill, const QStringList &targets)
{
    HintLedger *state = ledger(room, false);
    if (!state || !source || decisionId == 0) return;
    for (auto it = state->pending.begin(); it != state->pending.end();) {
        const QVariantMap row = it.value().toMap();
        if (row.value("source").toString() != source->objectName()
            || row.value("skill").toString() != skill
            || row.value("decision").toString() != QString::number(decisionId)) { ++it; continue; }
        const QString target = row.value("target").toString();
        if (targets.contains(target) && room.findPlayerByObjectName(target, true)) {
            state->visible.insert(it.key(), row.value("source"));
        }
        // Same accepted witness finishes this publication attempt; an absent
        // target cannot be revived by an unrelated later use of the same skill.
        state->acceptedCards.remove(it.key());
        it = state->pending.erase(it);
    }
}

bool stageAcceptedAIStrategyCardHints(Room &room, const AIRequest &request,
                                     const QJsonArray &effects, const Card *acceptedCard, QString *error, bool commit)
{
    const auto reject = [error](const QString &message) { if (error) *error = message; return false; };
    QJsonArray hints;
    for (const QJsonValue &value : effects) {
        const QJsonObject effect = value.toObject();
        if (effect.value("kind").toString() != "ai_player_hint") continue;
        const QString skill = skillForHint(effect.value("name").toString());
        if ((skill != "jiyu" && skill != "kuangbi") || !acceptedCard
            || acceptedCard->getActivationSkillName() != skill)
            return reject("hint does not belong to the accepted card product");
        hints.append(effect);
    }
    if (hints.isEmpty()) return true;
    if (request.stateRevision != room.roomRuntime()->stateRevision()) return reject("accepted card hint revision is stale");
    const SkillInstanceRef activation(request.viewerObjectName,
        SkillInstanceKey(acceptedCard->getActivationSkillName(), acceptedCard->getActivationSkillInstanceId()));
    const SkillInstanceRef root = room.resolveSkillInstanceRootRef(activation);
    const QVariantMap identity = cardHintIdentity(acceptedCard, activation, root);
    if (identity.isEmpty()) return reject("accepted card hint provenance is not a live complete source");
    if (!stageAIStrategyPlayerHints(room, request, hints, error, commit)) return false;
    if (!commit) return true;
    HintLedger *state = ledger(room, false);
    for (const QJsonValue &value : hints) {
        const QJsonObject effect = value.toObject();
        const QString key = entryKey(effect.value("name").toString(),effect.value("player").toString());
        state->acceptedCards.insert(key, acceptedCard);
        QVariantMap row = state->pending.value(key).toMap();
        row.insert("accepted_identity", identity); state->pending.insert(key, row);
    }
    return true;
}

void transferAcceptedAIStrategyCardHints(Room &room, const Card *acceptedCard,
    const Card *rebuiltCard, const ServerPlayer *source,
    const SkillInstanceRef &activationRef, const SkillInstanceRef &sourceRef)
{
    HintLedger *state = ledger(room, false);
    if (!state || !acceptedCard || !rebuiltCard || !source || source->getRoom() != &room
        || activationRef.ownerObjectName != source->objectName()) return;
    const QString skill = activationRef.key.skillName;
    if (skill != "jiyu" && skill != "kuangbi") return;
    const QVariantMap identity = cardHintIdentity(acceptedCard, activationRef, sourceRef);
    if (identity.isEmpty() || identity != cardHintIdentity(rebuiltCard, activationRef, sourceRef)) return;
    // This is a native-only successful-rebuild handoff, never a lookup by skill,
    // latest request, cost ids or caller-controlled Lua descriptor.
    for (auto it = state->pending.cbegin(); it != state->pending.cend(); ++it) {
        const QVariantMap row = it.value().toMap();
        if (state->acceptedCards.value(it.key()).data() == acceptedCard
            && row.value("source").toString() == source->objectName()
            && row.value("skill").toString() == skill
            && row.value("accepted_identity").toMap() == identity)
            state->acceptedCards[it.key()] = rebuiltCard;
    }
}

void witnessActualAIStrategyCardHints(Room &room, const Card *actualCard,
                                      const ServerPlayer *source, const QStringList &targets)
{
    HintLedger *state = ledger(room, false);
    if (!state || !actualCard || !source) return;
    const QString skill = actualCard->getActivationSkillName();
    if (skill != "jiyu" && skill != "kuangbi") return;
    QSet<quint64> decisions;
    for (auto it = state->pending.cbegin(); it != state->pending.cend(); ++it) {
        const QVariantMap row = it.value().toMap();
        if (state->acceptedCards.value(it.key()).data() == actualCard
            && row.value("source").toString() == source->objectName()
            && row.value("skill").toString() == skill)
            decisions.insert(row.value("decision").toString().toULongLong());
    }
    for (quint64 decision : decisions)
        witnessAIStrategyPlayerHints(room, decision, source, skill, targets);
}

bool stageAcceptedAIStrategyChoiceHints(Room &room, const AIRequest &request,
                                       const QJsonArray &effects, const ServerPlayer *choice,
                                       QString *error, bool commit)
{
    const auto reject = [error](const QString &message) { if (error) *error = message; return false; };
    QJsonArray hints;
    for (const QJsonValue &value : effects) {
        const QJsonObject effect = value.toObject();
        if (effect.value("kind").toString() != "ai_player_hint") continue;
        if (effect.value("name").toString() != "jiaozhaoAI" || !choice
            || effect.value("player").toString() != choice->objectName())
            return reject("hint does not belong to the accepted player choice");
        hints.append(effect);
    }
    if (hints.isEmpty()) return true;
    if (hints.size() != 1 || request.kind != AIRequest::PlayerChosen || request.choiceOptions.reason != "jiaozhao"
        || request.stateRevision != room.roomRuntime()->stateRevision())
        return reject("Jiaozhao accepted choice is outside its current question");
    const QVariantMap history = room.historyParent(room.currentHistoryEventId(), "skill", true).value("data").toMap();
    const qint64 execution = history.value("execution_id").toLongLong();
    const SkillExecutionRegistry::Entry *entry = room.findSkillExecution(execution);
    if (execution <= 0 || !entry || entry->finished) return reject("Jiaozhao live execution is missing");
    const SkillContext context = room.getSkillExecutionContext(execution);
    if (context.activationRef.key.skillName != "jiaozhao" || !context.initiator
        || context.initiator->objectName() != request.viewerObjectName || !context.use_card)
        return reject("Jiaozhao accepted choice belongs to another execution");
    if (!stageAIStrategyPlayerHints(room, request, hints, error, commit)) return false;
    if (!commit) return true;
    HintLedger *state = ledger(room, false);
    const QString key = entryKey("jiaozhaoAI", choice->objectName());
    QVariantMap pending = state->pending.value(key).toMap();
    pending.insert("execution", QString::number(execution));
    state->pending.insert(key,pending);
    state->acceptedCards.insert(key,context.use_card);
    return true;
}

void witnessAIStrategyJiaozhaoDeclaration(Room &room, const SkillContext &context,
                                         const ServerPlayer *target)
{
    HintLedger *state = ledger(room, false);
    if (!state || !target || !target->isAlive() || context.is_canceled
        || context.choice != "declare" || context.activationRef.key.skillName != "jiaozhao"
        || !context.initiator || !context.use_card || context.executionID <= 0
        || !context.targets.contains(const_cast<ServerPlayer *>(target))) return;
    const SkillExecutionRegistry::Entry *entry = room.findSkillExecution(context.executionID);
    if (!entry || entry->finished) return;
    const QString key = entryKey("jiaozhaoAI",target->objectName());
    const QVariantMap row = state->pending.value(key).toMap();
    if (row.value("execution").toString() != QString::number(context.executionID)
        || row.value("source").toString() != context.initiator->objectName()
        || state->acceptedCards.value(key).data() != context.use_card) return;
    witnessAIStrategyPlayerHints(room,row.value("decision").toString().toULongLong(),
        context.initiator,"jiaozhao",{target->objectName()});
}

QJsonObject consumeAIStrategyPlayerHint(Room &room, const AIRequest &request,
                                      ServerPlayer *viewer, const QString &name)
{
    const QString skill = skillForHint(name);
    if (!viewer || viewer->getRoom() != &room || request.viewerObjectName != viewer->objectName()
        || request.stateRevision != room.roomRuntime()->stateRevision() || skill.isEmpty())
        return failure("hint reader is outside its active viewer/revision");
    const AIRequest::DecisionKind expected = skill == "jiaozhao" ? AIRequest::AmazingGrace : AIRequest::Discard;
    if (request.kind != expected || request.choiceOptions.reason != skill)
        return failure("hint reader is outside its original question");
    HintLedger *state = ledger(room, false);
    const QString key = entryKey(name, viewer->objectName());
    const QString source = state ? state->visible.take(key).toString() : QString();
    if (source.isEmpty() || !room.findPlayerByObjectName(source, true))
        return {{"known", true}, {"source", false}};
    return {{"known", true}, {"source", source}};
}

namespace {
QJsonObject disclosedCard(const Card *card)
{
    if (!card) return {};
    QJsonArray kinds;
    for (const QString &kind : {QString("Card"), QString("BasicCard"), QString("TrickCard"),
        QString("DelayedTrick"), QString("EquipCard"), QString("SkillCard"), QString("Slash"),
        QString("Jink"), QString("Peach"), QString("Analeptic"), QString("Weapon"), QString("Armor"),
        QString("DefensiveHorse"), QString("OffensiveHorse"), QString("Treasure"), QString("AOE")})
        if (card->isKindOf(kind.toLatin1().constData())) kinds.append(kind);
    if (!kinds.contains(card->getClassName())) kinds.append(card->getClassName());
    QJsonArray costs; for (int id : card->getSubcards()) costs.append(id);
    return {{"id", card->getId()}, {"effective_id", card->getEffectiveId()},
        {"name", card->objectName()}, {"class_name", card->getClassName()}, {"kind_of", kinds},
        {"legacy_string_key", card->toString()}, {"legacy_sort_key", card->toString()},
        {"type_id", int(card->getTypeId())}, {"suit", int(card->getSuit())}, {"number", card->getNumber()},
        {"red", card->isRed()}, {"black", card->isBlack()}, {"virtual_card", card->isVirtualCard()},
        {"subcards", costs}, {"can_recast", card->canRecast()}, {"target_fixed", card->targetFixed()},
        {"damage_card", card->isDamageCard()}, {"handling_method", int(card->getHandlingMethod())},
        {"skill_name", card->getActivationSkillName()}, {"skill_names_complete", false}};
}
}

void projectAIStrategyYCZH2016(AIRequest &request, Room &room, ServerPlayer *viewer)
{
    if (!viewer || request.viewerObjectName != viewer->objectName()) return;
    QJsonObject facts, levels;
    // jiaozhaoSetLevel uses Room::setPlayerProperty plus a public mark. These
    // presentation levels, including the original invalid-property toInt()==0,
    // are public; selected-instance gameplay state is deliberately not exposed.
    for (ServerPlayer *player : room.getAllPlayers(true))
        levels.insert(player->objectName(), player->property("jiaozhao_level").toInt());
    facts.insert("jiaozhao_levels", levels);
    const int declared = viewer->getMark("jiaozhao_id-Clear") - 1;
    if (declared >= 0 && declared < Sanguosha->getCardCount())
        facts.insert("jiaozhao_declared_card", disclosedCard(Sanguosha->getEngineCard(declared)));
    else facts.insert("jiaozhao_declared_card", false);
    request.worldView.strategyFacts.insert("yczh2016", facts);

    if (request.kind == AIRequest::AmazingGrace && request.choiceOptions.reason == "jiaozhao") {
        QJsonObject printed;
        for (int id : request.choiceOptions.cardIds)
            if (id >= 0 && id < Sanguosha->getCardCount())
                printed.insert(QString::number(id), disclosedCard(Sanguosha->getEngineCard(id)));
        request.choiceOptions.context.insert("yczh2016_printed_cards", printed);
    }
    const bool offer = request.kind == AIRequest::UseCard && request.pattern == "@@huisheng";
    const bool discard = request.kind == AIRequest::Discard && request.choiceOptions.reason == "huisheng";
    if (offer || discard) {
        const QVariant value = room.getTag("HuishengDamage");
        if (value.canConvert<DamageStruct>()) {
            const DamageStruct damage = value.value<DamageStruct>();
            if ((offer && damage.to == viewer) || (discard && damage.from == viewer)) {
                QJsonObject row{{"from", damage.from ? damage.from->objectName() : QString()},
                    {"to", damage.to ? damage.to->objectName() : QString()},
                    {"amount", damage.damage}, {"nature", int(damage.nature)},
                    {"chain", damage.chain}, {"transfer", damage.transfer}, {"reason", damage.reason}};
                if (damage.card) row.insert("card", disclosedCard(damage.card));
                request.choiceOptions.context.insert("damage", row);
            }
        }
        if (discard) {
            // Audited writer YCZH2016::Huisheng::effectTarget sets this viewer's
            // own tag immediately after fillAG(ids, viewer), and restores it on
            // return. These faces were explicitly shown for this very question.
            QJsonArray ids; QJsonObject cards;
            const QVariantList offered = viewer->getTag("huisheng_ag_ids").toList();
            for (const QVariant &item : offered) {
                bool ok = false; const int id = item.toInt(&ok);
                if (!ok || id < 0 || id >= Sanguosha->getCardCount()) continue;
                ids.append(id); cards.insert(QString::number(id), disclosedCard(Sanguosha->getCard(id)));
            }
            request.choiceOptions.context.insert("huisheng_ag_ids", ids);
            request.choiceOptions.context.insert("huisheng_cards", cards);
        }
    }
}
